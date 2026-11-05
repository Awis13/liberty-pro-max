// Vehicle fixes and additions, for 1.0.8.0. Each one is an option (the launcher sets them), off unless said otherwise;
// GTAIV_TURN_INDICATORS_ROUTE is described by its code below:
//   GTAIV_TURN_INDICATORS (off by default): the player's vehicle shows its
//     indicators through a real turn: on once the wheel is over and the
//     vehicle has turned that way, or the wheel at full lock for a moment;
//     off as a car's stalk cancels, when the wheel has come back. Both only
//     near a junction, by the game's road nodes. A quick
//     flick of the pad's stick all the way over sets it before the turn, as
//     a stalk does: on through the turn, or for 12 s without one, or until
//     a flick the same way (on the move, with the stick at rest for a while
//     before it, longer without a junction ahead, as steering corrections
//     look the same); blinking
//     as the game's own hazard lights do; a crash hard enough to throw
//     someone through the windscreen puts the hazard lights on until the
//     player next gets into that vehicle (after leaving it, if he was the
//     one in it);
//   GTAIV_TURN_INDICATORS_MANUAL (off by default, with the above): the
//     indicators by hand instead: a tap of [ or ] (or of the pad's left or
//     right bumper) switches the left or right one on or off, a tap of
//     both together the hazard lights;
//   GTAIV_KEEP_WHEELS_TURNED (off by default): a vehicle the player leaves
//     keeps its front wheels (or handlebars) turned as he left them, not
//     straightened.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "hook_util.h"
#include "runtime/log.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

namespace {

constexpr gaddr kClock = 0x010FCB6C;        // the game's clock, ms
constexpr gaddr kFrame = 0x010FCC30;        // CTimer's frame counter
constexpr gaddr kVehiclePool = 0x01252CD0;  // the CVehicle pool (objects, flags, count, size)

// CVehicle fields (each seen where the game itself uses it).
constexpr uint32_t kEngineFlags = 0xF64; // 08 engine on (the light pass lights no turn signal without it)
constexpr uint32_t kLightFlags = 0xF6C;  // 02 hazard lights (SET_VEH_HAZARDLIGHTS)
constexpr uint32_t kOwnFlags = 0xF66;    // 02 has been the player's (SET_HAS_BEEN_OWNED_BY_PLAYER)
constexpr uint32_t kDriver = 0xFA0;      // the driving CPed
constexpr uint32_t kHandling = 0xE18;    // handling data; its +84 the steering lock
constexpr uint32_t kSteerAngle = 0x10D8; // the front wheels' angle: steer * lock
constexpr uint32_t kBlinkOffset = 0x2C;  // word added to the clock for the blink phase

constexpr uint32_t kNone = ~0u;
constexpr float kPi = 3.14159265f;

// The player's CPed: the reads FindPlayerPed (007CD230) makes.
uint32_t PlayerPed() {
  const uint32_t i = Rd32(0x00F2E0B4);
  if (i == kNone) return 0;
  const uint32_t info = Rd32(0x01100498 + 4 * i);
  return info ? Rd32(info + 0x58C) : 0;
}

// The vehicle the player is all the way in (007CD270's reads), or 0.
uint32_t PlayerVehicle(uint32_t ped) {
  return ped && (Rd8(ped + 0x26C) & 4) ? Rd32(ped + 0xB40) : 0;
}

// A vehicle's pool handle (slot << 8 | the slot's flag byte) and back, as
// the pool's GetAt (00492E00) checks it: a vehicle gone (its slot freed or
// used again) no longer matches.
uint32_t Handle(uint32_t veh) {
  const uint32_t pool = Rd32(kVehiclePool);
  if (!pool || !veh) return kNone;
  const uint32_t base = Rd32(pool), size = Rd32(pool + 0xC);
  if (!size || veh < base || (veh - base) % size) return kNone;
  const uint32_t slot = (veh - base) / size;
  if (slot >= Rd32(pool + 8)) return kNone;
  return slot << 8 | Rd8(Rd32(pool + 4) + slot);
}

uint32_t VehicleAt(uint32_t h) {
  const uint32_t pool = Rd32(kVehiclePool);
  if (h == kNone || !pool) return 0;
  const uint32_t slot = h >> 8;
  if (slot >= Rd32(pool + 8)) return 0;
  if (Rd8(Rd32(pool + 4) + slot) != (h & 0xFF)) return 0;
  return Rd32(pool) + slot * Rd32(pool + 0xC);
}

void SetHazard(uint32_t veh, bool on) {
  const uint8_t f = Rd8(veh + kLightFlags);
  Wr8(veh + kLightFlags, on ? uint8_t(f | 2) : uint8_t(f & ~2));
}

// The game's blink phase for a vehicle's hazard lights (the light pass,
// 008FC025): on for 512 ms, off for 512.
bool BlinkOn(uint32_t veh, uint32_t clock) {
  return ((Rd16(veh + kBlinkOffset) + clock) >> 9) & 1;
}

bool TurnIndicatorsOn() {
  static const bool on = OptionOn("GTAIV_TURN_INDICATORS", false);
  return on;
}

bool ManualIndicators() {
  static const bool on = OptionOn("GTAIV_TURN_INDICATORS_MANUAL", false);
  return on;
}

// How far over the wheel must be (a share of the full lock) for a turn,
// and how nearly straight it must be again for that turn to be over. The
// vehicle must then have turned that far (degrees, going forwards, faster
// than this in m/s), so that a correction shows nothing, and quickly enough
// (degrees a second, averaged over a moment), so that a curving street
// shows nothing; further the other way soon after an indicator, which is
// mostly the wheel being straightened.
// Or the wheel nearly at full lock for a moment, standing or not, but not
// in reverse: longer the other way, for the same reason.
// Once on, at least three blinks, and off when the wheel has been back for
// a moment, as a stalk cancels. A tap is a press this short.
constexpr float kTurnOnShare = 0.18f;
constexpr float kTurnOffShare = 0.08f;
constexpr float kTurnStartDeg = 20.0f;
constexpr float kTurnEasySpeed = 25.0f / 3.6f, kTurnHardSpeed = 60.0f / 3.6f, kTurnHardExtraDeg = 20.0f;
constexpr float kTurnBackDeg = 35.0f;
constexpr float kTurnBackWindowMs = 3000.0f;
constexpr float kTurnMinSpeed = 1.0f;
constexpr float kTurnMinRate = 20.0f;
constexpr float kTurnRateMs = 200.0f;
constexpr float kTurnFadeRate = 10.0f;
constexpr float kTurnFullShare = 0.85f;
constexpr float kTurnFullMs = 500.0f;
constexpr float kTurnFullBackMs = 1000.0f;
constexpr float kTurnMinOnMs = 3072.0f;
constexpr float kTurnBackMs = 400.0f;
// A flick: from rest (past kFlickRest, after this long there: a correction
// comes among others) all the way over (past kFlickFull) and back, this
// quickly, standing or not. The stick's -127 to 127.
constexpr int kFlickRest = 40;
constexpr int kFlickFull = 110;
constexpr float kFlickMs = 300.0f;
constexpr float kFlickRestMs = 800.0f;
// On the move a quick correction looks just like a flick, and the stick is
// rarely still for long. So how long at rest it takes goes by the speed:
// nothing when stopped; the usual below kFlickSlow or with a junction ahead;
// longer without one (not to change or cancel one set). And a correction's
// other half, full over the other way this soon after, takes it back.
constexpr float kFlickStopped = 5.0f / 3.6f;  // m/s
constexpr float kFlickSlow = 20.0f / 3.6f;
constexpr float kFlickFastest = 50.0f / 3.6f;
constexpr float kFlickMidRestMs = 1700.0f;
constexpr float kFlickAheadSeconds = 5.0f, kFlickAheadMin = 30.0f, kFlickAheadMax = 120.0f;
constexpr float kFlickCancelMs = 700.0f;
constexpr float kTurnLatchMs = 12000.0f;
// A turn counts near a junction only: a node of 3+ links, within this distance
// (m) and not far behind.
// Asked only when a turn would start, and not more often than this.
constexpr float kJunctionRange = 30.0f;
constexpr float kJunctionBehind = 10.0f;
constexpr float kJunctionAskMs = 400.0f;
constexpr float kTapMs = 300.0f;

// The player's vehicle's indicator (0 none, 1 left, 2 right) and what
// decides it.
struct Indicators {
  uint32_t car = kNone;   // the vehicle's handle
  uint32_t ptr = 0;       // and the vehicle this frame
  int side = 0;
  bool stopping = false;  // off once this blink is over
  int turn = 0;           // automatic: the way the wheel is over
  float turned = 0;       //   how far the vehicle has turned that way (degrees)
  float rate = 0;         //   and how quickly (degrees a second)
  bool seen = false;      //   its place and heading last frame:
  float x = 0, y = 0, heading = 0;
  float full_ms = 0;      //   the wheel nearly at full lock this long
  bool junction = false;  //   near a junction, as last asked
  float asked_ms = 1e9f;  //     this long ago
  float on_ms = 0;        //   the indicator's been on this long
  float back_ms = 0;      //   with the wheel back this long
  int last_side = 0;      //   the last indicator
  float last_ms = 0;      //   off this long
  bool real_turn = false; //   the last one was a turn, not a flick
  bool latched = false;   //   set by a flick: on until the turn is made
  float latch_ms = 0;     //     for this long
  int flick_dir = 0;      //   the stick is off its rest this way (3 not a flick)
  float flick_ms = 0;     //     for this long
  bool flick_full = false;  //   and went all the way
  float rest_ms = 0;      //   at rest this long
  float flick_rest_ms = 0;  //   at rest this long before it left
  bool flick_cancel = false;  //   the one just set was a correction
  float forward = 0;      //   m/s, last frame
  float z = 0;            //   and its height
  bool left = false, right = false;  // manual: what is down
  float left_ms = 0, right_ms = 0;   //   since when
  bool both = false;                 //   both were down together
  bool route_on = false;  // on because the GPS route turns ahead, or ends
  bool route_arrive = false;  //   ends: towards the kerb
  int route_side = 0;
  float route_x = 0, route_y = 0, route_near = 1e9f;  //   the turn, and how near it has come
  float skip_x = 1e9f, skip_y = 1e9f;                 //   a turn he said no to
  uint32_t route_slot = 0;  //   whose route
  int logged_side = 0;      // the last side written to the log
  float since_stop = 0;     // metres driven since it was last parked
  float stood_ms = 0;       // standing this long
};

// A vehicle whose hazard lights a crash put on; wait_exit while the
// player who crashed it is still in it.
struct Crash {
  uint32_t car;
  bool wait_exit;
};

std::mutex g_mu;
Indicators g_ind;
std::vector<Crash> g_crashes;
uint32_t g_frame = kNone, g_clock = 0;
bool g_clock_set = false;

void DropCrash(uint32_t h) {
  for (size_t i = 0; i < g_crashes.size(); i++)
    if (g_crashes[i].car == h) {
      g_crashes.erase(g_crashes.begin() + i);
      return;
    }
}

// The pad's button (IS_BUTTON_PRESSED's check, 00B4CF30) or the keyboard's
// key (DirectInput code; the game's own key check, 008A0600, as its
// keyboard bindings use it). Guest calls where the game makes none.
bool ButtonDown(Ctx& c, uint32_t button, uint32_t key) {
  bool down;
  {
    KeepRegs keep(c);
    down = (CallGuest(c, 0x00B4CF30, {0, button}) & 0xFF) != 0;
    if (!down) {
      c.ecx = 0x011E78C0;
      down = (CallGuest(c, 0x008A0600, {key, 1, 0}) & 0xFF) != 0;
    }
  }
  return down;
}

bool TurnLog() {
  static const bool on = OptionOn("GTAIV_TURN_LOG", false);
  return on;
}

// The side (1 left, 2 right) the pad's left stick has just been flicked,
// or 0. IS_USING_CONTROLLER's test and GET_PAD_STATE's lookup (control 0,
// the left stick's x; see camera_controls.cpp). A keyboard's tap is a
// correction, and a flick is the pad's.
bool JunctionNear(Ctx& c, float x, float y, float z, float fx, float fy);

// How long at rest a flick needs to have been, as the vehicle is now.
float FlickRestNeeded(Ctx& c) {
  Indicators& s = g_ind;
  if (s.latched || s.forward <= kFlickStopped) return s.latched ? kFlickRestMs : 0.0f;
  if (s.forward <= kFlickSlow || s.rest_ms < kFlickRestMs) return kFlickRestMs;
  const float fx = std::cos(s.heading), fy = std::sin(s.heading);
  const float far = std::clamp(s.forward * kFlickAheadSeconds, kFlickAheadMin, kFlickAheadMax);
  for (float d = kFlickAheadMin; d <= far; d += kFlickAheadMin)
    if (JunctionNear(c, s.x + fx * d, s.y + fy * d, s.z, fx, fy)) return kFlickRestMs;
  return kFlickMidRestMs;
}

int Flick(Ctx& c, float dt) {
  Indicators& s = g_ind;
  if (dt <= 0) return 0;  // paused: the stick is working a menu
  int raw = 0;
  {
    KeepRegs keep(c);
    if (uint8_t(CallGuest(c, 0x00B4D3D0, {}))) raw = int32_t(CallGuest(c, 0x00B4CD40, {0, 0}));
  }
  int out = 0;
  if (std::abs(raw) < kFlickRest) {
    if (s.flick_dir) {
      if (s.flick_dir != 3 && s.flick_full && s.flick_ms <= kFlickMs) out = s.flick_dir;
      if (TurnLog()) std::fprintf(stderr, "turn: stick back, dir %d full %d %.0f ms rest %.0f ms %.0f km/h -> %d\n", s.flick_dir,
                                  s.flick_full, s.flick_ms, s.flick_rest_ms, s.forward * 3.6f, out);
      s.flick_dir = 0;
      s.flick_full = false;
      s.rest_ms = 0;
    }
    s.rest_ms += dt;
    return out;
  }
  const int dir = raw < 0 ? 1 : 2;
  if (!s.flick_dir) {
    // (Fast, it is a correction: on the road a player's log showed nothing else.)
    s.flick_dir = s.forward <= kFlickFastest && s.rest_ms >= FlickRestNeeded(c) ? dir : 3;
    s.flick_rest_ms = s.rest_ms;
    s.flick_ms = 0;
  } else if (s.flick_dir != dir) {
    s.flick_dir = 3;
  }
  s.flick_ms += dt;
  if (std::abs(raw) >= kFlickFull) {
    s.flick_full = true;
    if (s.latched && s.side && dir != s.side && s.latch_ms < kFlickCancelMs) s.flick_cancel = true;
  }
  return 0;
}

constexpr gaddr kAreaNodes = 0x0121CF24, kAreaCount = 0x0121D224;

// Whether a junction is near the vehicle (at x, y, forward fx, fy): a road node of 3+
// links (+1E, the low 4 bits), within this distance and not far behind, at any angle:
// the areas' nodes (0121CF24, a grid of 750 m from -3000) as the game's AI reads them.
// Nodes in boats' routes (height about 0) are not roads. None loaded counts as yes, so
// as not to lose the indicator.
constexpr float kBoatRoute = 1.0f;
constexpr float kJunctionLevel = 6.0f;  // m: more above or below is a road over or under this one

bool JunctionNear(Ctx&, float x, float y, float z, float fx, float fy) {
  bool any = false;
  for (int ay = int((y - kJunctionRange + 3000) / 750); ay <= int((y + kJunctionRange + 3000) / 750); ay++) {
    for (int ax = int((x - kJunctionRange + 3000) / 750); ax <= int((x + kJunctionRange + 3000) / 750); ax++) {
      if (ax < 0 || ay < 0 || ax > 7 || ay > 7) continue;
      const uint32_t area = uint32_t(ax + 8 * ay), nodes = Rd32(kAreaNodes + area * 4), count = Rd32(kAreaCount + area * 4);
      if (!nodes) continue;
      any = true;
      for (uint32_t i = 0; i < count; i++) {
        const uint32_t n = nodes + i * 0x20;
        if ((Rd8(n + 0x1E) & 0xF) < 3 || (Rd8(n + 0x1C) >> 4) != 0 || (Rd8(n + 0x1E) & 0x80)) continue;
        const float dx = float(int16_t(Rd16(n + 0x14))) * 0.125f - x, dy = float(int16_t(Rd16(n + 0x16))) * 0.125f - y;
        if (dx * dx + dy * dy > kJunctionRange * kJunctionRange) continue;
        const float node_z = float(int16_t(Rd16(n + 0x18))) * 0.015625f;
        if (node_z < kBoatRoute || std::fabs(node_z - z) > kJunctionLevel) continue;
        if (dx * fx + dy * fy < -kJunctionBehind) continue;
        return true;
      }
    }
  }
  return !any;
}

// GTAIV_TURN_INDICATORS_ROUTE (off by default, with the automatic ones): with a
// GPS route, the indicator comes on before the route's turn, up to 40 m ahead as
// the game's AI does (00C5C330), at a node of 3+ links, the side from the cross
// product. Unlike the AI, which takes a turn only from 75.5 degrees, a bend of 37
// degrees counts: on a fork at an angle the hint is what he needs. Near the end of the
// route, towards the kerb, until he stops or drives on. A route's points are thinned, to
// within about 7 degrees; a byte for each at +C80 is the node's links. The scripts' route
// first (a mission's), then the waypoint's.
bool RouteIndicatorsOn() {
  static const bool on = OptionOn("GTAIV_TURN_INDICATORS_ROUTE", false);
  return on;
}

constexpr uint32_t kRouteSlots[] = {0x011F72A0, 0x011F64F0};
constexpr uint32_t kRouteLinks = 0xC80, kRouteTarget = 0xD50, kRouteCount = 0xD60, kRouteState = 0xD64, kRouteOffLine = 0xD9C;
constexpr uint32_t kRouteReady = 3, kRouteRedo = 1, kRouteRedoNow = 2, kRouteMostPoints = 200;
constexpr float kRouteAhead = 40.0f;
constexpr float kRouteOffMost = 25.0f;  // m from the route's line: past this the game's GPS says he has left it (00892870)
constexpr float kRouteFastest = 70.0f;  // m/s: more is a teleport's one frame
// The wheel the other way, this far and this far over, by the junction: he knows better.
constexpr float kRouteOverrideDeg = 25.0f, kRouteOverrideShare = 0.5f, kRouteOverrideNear = 25.0f;
// Pulling away: parked by the kerb 15 m from the route's right turn, the car has to leave
// the kerb first, so not before this far from where it stood this long. A shorter stand is
// traffic, or a light, and the hint is as good after it as before.
constexpr float kRouteAfterStart = 20.0f, kRouteParkedMs = 10000.0f;
constexpr float kRoutePast = 8.0f;     // this much further than it came: the turn is behind
constexpr float kRouteForgetSkip = 30.0f;  // a turn he said no to, once this far from it

// The way into and out of a junction, along the route, this far each way. Not from the
// next point alone: a wide junction is often two nodes a few metres apart, and the short
// link between them points anywhere. A turn is 37 degrees or more: streets at an angle
// count, a highway's exit at 20 does not. The way in must be about how the car is going:
// the game keeps a route up to 50 m off it, so he may be coming along another street.
// Nearly straight back is a U-turn, whose cross product says nothing: to the left.
constexpr float kRouteLook = 25.0f, kRouteTurnCos = 0.8f, kRouteSameJunction = 20.0f;
constexpr float kRouteHeadingCos = 0.7f, kRouteUTurnCos = -0.9f, kRouteCloseNode = 15.0f;
// The end of the route: how far before it, and how far aside the target has to be to count
// as off this road. The game takes the route off 20 m from its end (00891D80), so it has to
// be earlier than that. Stopped this long, he has arrived.
constexpr float kArriveAhead = 40.0f, kArriveOnRoad = 6.0f, kArriveStopMs = 1000.0f;

float PointX(uint32_t slot, int i) { return RdF32(slot + uint32_t(i) * 16); }
float PointY(uint32_t slot, int i) { return RdF32(slot + uint32_t(i) * 16 + 4); }

// The place this far along the route's points from point i, going by step; returns how far
// the points went.
float AlongRoute(uint32_t slot, int i, int step, int count, float* x, float* y) {
  float left = kRouteLook;
  float ax = PointX(slot, i), ay = PointY(slot, i);
  for (int k = i + step; k >= 0 && k < count; k += step) {
    const float bx = PointX(slot, k), by = PointY(slot, k);
    const float len = std::hypot(bx - ax, by - ay);
    if (len >= left) {
      *x = ax + (bx - ax) * left / len;
      *y = ay + (by - ay) * left / len;
      return kRouteLook;
    }
    left -= len;
    ax = bx;
    ay = by;
  }
  *x = ax;
  *y = ay;
  return kRouteLook - left;
}

// Whether the route turns at point i for a car here, and which way. Where the points do not
// go far enough back (the game trims those behind the car), the way in is the car's own, but
// not close to the node: there the car is already steering through the junction, and its
// heading says nothing about the road. Too late for a hint anyway.
bool TurnAt(uint32_t slot, int i, int count, float px, float py, float fx, float fy, int* side) {
  const float x = PointX(slot, i), y = PointY(slot, i);
  float bx = px - fx * kRouteLook, by = py - fy * kRouteLook, ax, ay, ox, oy;
  if (i && AlongRoute(slot, i, -1, count, &ax, &ay) >= kRouteLook) {
    bx = ax;
    by = ay;
  } else if (std::hypot(x - px, y - py) < kRouteCloseNode) {
    return false;
  }
  AlongRoute(slot, i, 1, count, &ox, &oy);
  float ux = x - bx, uy = y - by, vx = ox - x, vy = oy - y;
  const float lu = std::max(std::hypot(ux, uy), 0.01f), lv = std::max(std::hypot(vx, vy), 0.01f);
  ux /= lu;
  uy /= lu;
  vx /= lv;
  vy /= lv;
  if (ux * fx + uy * fy < kRouteHeadingCos) return false;
  const float cosine = ux * vx + uy * vy;
  if (cosine >= kRouteTurnCos) return false;
  *side = cosine < kRouteUTurnCos || vy * ux - uy * vx >= 0 ? 1 : 2;
  return true;
}

struct RouteTurn {
  int side;
  float x, y;
  uint32_t slot;
};

// A route the indicator can go by: the first ready one, its points, not left behind.
bool ReadyRoute(uint32_t* slot, int* count) {
  for (const uint32_t r : kRouteSlots) {
    const uint32_t n = Rd32(r + kRouteCount);
    if (Rd32(r + kRouteState) != kRouteReady || n < 2 || n > kRouteMostPoints) continue;
    if (RdF32(r + kRouteOffLine) >= kRouteOffMost) return false;
    *slot = r;
    *count = int(n);
    return true;
  }
  return false;
}

// Where the route really bends, of the nodes of a junction starting at point i: the turn
// is over only past that one.
void SharpestNear(uint32_t slot, int i, int count, float px, float py, float* x, float* y) {
  const float ix = PointX(slot, i), iy = PointY(slot, i);
  float least = 2;
  *x = ix;
  *y = iy;
  for (int k = i; k + 1 < count; k++) {
    const float cx = PointX(slot, k), cy = PointY(slot, k);
    if (std::hypot(cx - ix, cy - iy) > kRouteSameJunction) break;
    const float ax = k ? PointX(slot, k - 1) : px, ay = k ? PointY(slot, k - 1) : py;
    const float bx = PointX(slot, k + 1), by = PointY(slot, k + 1);
    const float lu = std::max(std::hypot(cx - ax, cy - ay), 0.01f), lv = std::max(std::hypot(bx - cx, by - cy), 0.01f);
    const float cosine = ((cx - ax) * (bx - cx) + (cy - ay) * (by - cy)) / (lu * lv);
    if (cosine < least) {
      least = cosine;
      *x = cx;
      *y = cy;
    }
  }
}

bool NextRouteTurn(float px, float py, float fx, float fy, RouteTurn* out) {
  uint32_t slot;
  int count;
  if (!ReadyRoute(&slot, &count)) return false;
  float way = 0, lx = px, ly = py;
  bool started = false;
  for (int i = 0; i + 1 < count; i++) {
    const float x = PointX(slot, i), y = PointY(slot, i);
    if (!started && (x - px) * fx + (y - py) * fy < 0) continue;  // the first points can be behind
    started = true;
    way += std::hypot(x - lx, y - ly);
    lx = x;
    ly = y;
    if (way > kRouteAhead) return false;
    if ((Rd8(slot + kRouteLinks + uint32_t(i)) & 0x7F) <= 2) continue;
    int side;
    if (!TurnAt(slot, i, count, px, py, fx, fy, &side)) continue;
    out->side = side;
    out->slot = slot;
    SharpestNear(slot, i, count, px, py, &out->x, &out->y);
    return true;
  }
  return false;
}

// The route's end, if it is near: to the side its target lies on, off the road, or else
// to the right.
bool RouteEnd(float px, float py, float fx, float fy, RouteTurn* out) {
  uint32_t slot;
  int count;
  if (!ReadyRoute(&slot, &count)) return false;
  float way = 0, lx = px, ly = py;
  bool started = false;
  for (int i = 0; i < count; i++) {
    const float x = PointX(slot, i), y = PointY(slot, i);
    if (!started && (x - px) * fx + (y - py) * fy < 0) continue;
    started = true;
    way += std::hypot(x - lx, y - ly);
    lx = x;
    ly = y;
    if (way > kArriveAhead) return false;
  }
  if (!started) return false;
  const float ex = PointX(slot, count - 1), ey = PointY(slot, count - 1);
  float ux = ex - PointX(slot, count - 2), uy = ey - PointY(slot, count - 2);
  const float len = std::hypot(ux, uy);
  if (len < 0.01f) return false;
  ux /= len;
  uy /= len;
  const float right = (RdF32(slot + kRouteTarget) - ex) * uy - (RdF32(slot + kRouteTarget + 4) - ey) * ux;
  *out = {right < -kArriveOnRoad ? 1 : 2, ex, ey, slot};
  return true;
}

// Whether the route on the indicator still turns that way at that place. Being worked out
// again it keeps its say for now (up to 3 s). Close to the turn, the car's heading and the
// trimmed points tell nothing: the distance decides that.
bool RouteStillTurns(const Indicators& s, float px, float py, float fx, float fy, float d) {
  const uint32_t state = Rd32(s.route_slot + kRouteState), n = Rd32(s.route_slot + kRouteCount);
  if (state == kRouteRedo || state == kRouteRedoNow) return true;
  if (state != kRouteReady || n < 2 || n > kRouteMostPoints) return false;
  if (d <= kRouteCloseNode) return true;
  for (int i = 0; i + 1 < int(n); i++) {
    if (std::hypot(PointX(s.route_slot, i) - s.route_x, PointY(s.route_slot, i) - s.route_y) > kRouteSameJunction) continue;
    int side;
    if (TurnAt(s.route_slot, i, int(n), px, py, fx, fy, &side) && side == s.route_side) return true;
  }
  return false;
}

// A new route's end somewhere else ends the arriving; the game taking this one off does not.
bool RouteStillEnds(const Indicators& s) {
  const uint32_t n = Rd32(s.route_slot + kRouteCount);
  if (Rd32(s.route_slot + kRouteState) != kRouteReady || n < 2 || n > kRouteMostPoints) return true;
  return std::hypot(PointX(s.route_slot, int(n) - 1) - s.route_x, PointY(s.route_slot, int(n) - 1) - s.route_y) <= kRouteSameJunction;
}

// The route has no more say on this turn; off, if nothing else holds it.
void DropRouteHint(Indicators& s, bool off) {
  s.route_on = s.route_arrive = false;
  s.skip_x = s.route_x;
  s.skip_y = s.route_y;
  if (off) s.on_ms = std::max(s.on_ms, kTurnMinOnMs);
}

// The route's say on the indicator, for this frame.
void RouteIndicator(float px, float py, float fx, float fy, float forward) {
  Indicators& s = g_ind;
  if (std::hypot(s.skip_x - px, s.skip_y - py) > kRouteForgetSkip) s.skip_x = s.skip_y = 1e9f;
  if (s.route_on) {
    const float d = std::hypot(s.route_x - px, s.route_y - py);
    s.route_near = std::min(s.route_near, d);
    if (s.side != s.route_side) {  // he steered or flicked the other way
      DropRouteHint(s, false);
    } else if (d > s.route_near + kRoutePast || forward < -kTurnMinSpeed) {  // behind now: the wheel has the rest
      DropRouteHint(s, true);
    } else if (s.route_arrive ? s.stood_ms >= kArriveStopMs || !RouteStillEnds(s) : !RouteStillTurns(s, px, py, fx, fy, d)) {
      DropRouteHint(s, true);
    }
    if (s.route_on) return;
  }
  if (s.since_stop < kRouteAfterStart) return;
  if (forward < -kTurnMinSpeed || forward > kRouteFastest) return;
  RouteTurn t;
  bool arrive = false;
  if (!NextRouteTurn(px, py, fx, fy, &t)) {
    if (!RouteEnd(px, py, fx, fy, &t)) return;
    arrive = true;
  }
  if (std::hypot(t.x - s.skip_x, t.y - s.skip_y) < kRouteSameJunction) return;  // (its other node, too)
  if (s.side && s.side != t.side) return;  // his, or a turn still ending
  if (!s.side) s.on_ms = s.back_ms = 0;
  s.side = t.side;
  s.latched = false;
  s.stopping = false;
  s.real_turn = false;
  s.route_on = true;
  s.route_arrive = arrive;
  s.route_side = t.side;
  s.route_x = t.x;
  s.route_y = t.y;
  s.route_slot = t.slot;
  s.route_near = std::hypot(t.x - px, t.y - py);
}

void Automatic(Ctx& c, uint32_t veh, float dt) {
  Indicators& s = g_ind;
  const int flick = Flick(c, dt);
  const float lock = RdF32(Rd32(veh + kHandling) + 0x84);
  const float angle = RdF32(veh + kSteerAngle);
  const float share = lock > 0 ? angle / lock : 0.0f;
  int turn = 0;  // the angle is positive to the left
  if (share > (s.turn == 1 ? kTurnOffShare : kTurnOnShare)) turn = 1;
  else if (share < -(s.turn == 2 ? kTurnOffShare : kTurnOnShare)) turn = 2;
  if (turn != s.turn) {
    s.turn = turn;
    s.turned = 0;
    s.rate = 0;
    s.full_ms = 0;
    s.asked_ms = 1e9f;
  }
  s.asked_ms += dt;
  bool reversing = false;
  float forward = 0, fx = 0, fy = 1, px = 0, py = 0, pz = 0;
  const uint32_t mat = Rd32(veh + 0x20);
  if (mat && dt > 0) {
    fx = RdF32(mat + 0x10);
    fy = RdF32(mat + 0x14);
    px = RdF32(mat + 0x30);
    py = RdF32(mat + 0x34);
    pz = RdF32(mat + 0x38);
    s.z = pz;
    const float heading = std::atan2(fy, fx);  // grows to the left
    if (s.seen) forward = ((px - s.x) * fx + (py - s.y) * fy) / dt * 1000;
    if (s.seen && turn) {
      float d = heading - s.heading;
      if (d > kPi) d -= 2 * kPi;
      if (d < -kPi) d += 2 * kPi;
      reversing = forward < -kTurnMinSpeed;
      if (forward > kTurnMinSpeed && std::fabs(d) < 0.5f) {  // not a teleport
        const float deg = (turn == 1 ? d : -d) * 180 / kPi;
        s.rate += (deg / dt * 1000 - s.rate) * std::min(dt / kTurnRateMs, 1.0f);
        if (s.rate >= kTurnMinRate) s.turned += deg;
        else s.turned = std::max(s.turned - kTurnFadeRate * dt / 1000, 0.0f);
      } else {
        s.rate = 0;
      }
    }
    s.seen = true;
    s.x = px;
    s.y = py;
    s.heading = heading;
  }
  if (s.side) {
    s.on_ms += dt;
    s.back_ms = turn == s.side ? 0 : s.back_ms + dt;
    s.last_side = s.side;
    s.last_ms = 0;
  } else {
    s.last_ms += dt;
  }
  s.forward = forward;
  s.full_ms = turn && std::fabs(share) >= kTurnFullShare && !reversing ? s.full_ms + dt : 0;
  if (s.flick_cancel) {
    s.flick_cancel = false;
    if (s.latched) {
      if (TurnLog()) std::fprintf(stderr, "turn: flick taken back\n");
      s.latched = s.real_turn = false;
      s.on_ms = kTurnMinOnMs;
      s.back_ms = kTurnBackMs;
    }
  }
  const bool route_was_on = flick && s.route_on;
  if (route_was_on) DropRouteHint(s, false);  // no to the route's turn: this one stays his
  if (flick && s.side == flick) {  // the same way again: off
    s.latched = s.real_turn = false;
    s.on_ms = kTurnMinOnMs;
    s.back_ms = kTurnBackMs;
  } else if (flick && (route_was_on || s.latched || (!s.side && !(s.real_turn && s.last_ms < kTurnBackWindowMs)))) {
    // (Not while a turn's indicator is on or just over: the wheel being
    // straightened can look like a flick.)
    s.side = flick;
    s.latched = true;
    s.latch_ms = s.on_ms = s.back_ms = 0;
    s.real_turn = false;
  }
  const bool back = s.last_side && turn != s.last_side && s.last_ms < kTurnBackWindowMs;
  // The faster, the more it takes: weaving through traffic turns the car too, and a real
  // turn at speed gets to it anyway.
  const float start_deg = kTurnStartDeg + std::clamp((forward - kTurnEasySpeed) / (kTurnHardSpeed - kTurnEasySpeed), 0.0f, 1.0f) * kTurnHardExtraDeg;
  bool turning = turn && (s.turned >= (back ? std::max(kTurnBackDeg, start_deg) : start_deg) ||
                          s.full_ms >= (back ? kTurnFullBackMs : kTurnFullMs));
  // The route said one way and the wheel goes the other: at once, wherever this is.
  const bool route_overruled = s.route_on && turn && turn != s.side && s.turned >= kRouteOverrideDeg &&
                               std::fabs(share) >= kRouteOverrideShare && std::hypot(s.route_x - px, s.route_y - py) <= kRouteOverrideNear;
  if (route_overruled) turning = true;
  if (turning && turn != s.side && mat && !route_overruled) {
    if (s.asked_ms >= kJunctionAskMs) {
      s.junction = JunctionNear(c, px, py, pz, fx, fy);
      s.asked_ms = 0;
    }
    turning = s.junction;
  }
  if (turning) {
    if (turn != s.side) s.on_ms = s.back_ms = 0;
    s.side = turn;
    s.latched = false;
    s.real_turn = true;
  } else if (s.latched && (s.latch_ms += dt) >= kTurnLatchMs) {
    s.latched = false;
  }
  if (dt > 0) {  // (paused, nothing here has been read)
    s.stood_ms = forward < kTurnMinSpeed ? s.stood_ms + dt : 0;
    if (s.stood_ms >= kRouteParkedMs) s.since_stop = 0;
    else if (forward > 0) s.since_stop += std::min(forward, kRouteFastest) * dt / 1000;
    if (RouteIndicatorsOn() && mat) RouteIndicator(px, py, fx, fy, forward);
  }
  // Each change, with why: so that a wrong one can be told from the log afterwards.
  if (s.side != s.logged_side) {
    s.logged_side = s.side;
    WarnF("turn: %s by %s at %.0f,%.0f heading %.0f %.0f km/h wheel %+.2f turned %.0f rate %.0f junction %d corner %.0f,%.0f\n",
          s.side == 1 ? "left" : s.side == 2 ? "right" : "off",
          s.route_arrive ? "arriving" : s.route_on ? (s.route_slot == kRouteSlots[0] ? "script route" : "waypoint route") : s.latched ? "flick" : s.real_turn ? "wheel" : "-", px, py,
          std::atan2(fy, fx) * 180 / kPi, forward * 3.6f, share, s.turned, s.rate, s.junction, s.route_on ? s.route_x : 0.0f, s.route_on ? s.route_y : 0.0f);
  }
  s.stopping = s.side && !s.latched && !s.route_on && s.on_ms >= kTurnMinOnMs && s.back_ms >= kTurnBackMs;

  // GTAIV_TURN_LOG=1: the wheel and the speed, four times a second.
  if (TurnLog()) {
    static float since = 0, px = 0, py = 0;
    since += dt;
    if (since >= 250) {
      const uint32_t m = Rd32(veh + 0x20);
      const float x = m ? RdF32(m + 0x30) : 0, y = m ? RdF32(m + 0x34) : 0;
      const float kmh = std::hypot(x - px, y - py) / (since / 1000) * 3.6f;
      std::fprintf(stderr, "turn: share %+.2f lock %.2f speed %.0f km/h side %d turned %.0f rate %.0f\n", share, lock, kmh, s.side, s.turned, s.rate);
      px = x;
      py = y;
      since = 0;
    }
  }
}

void Manual(Ctx& c, uint32_t veh, float dt) {
  Indicators& s = g_ind;
  if (dt <= 0) return;  // paused: the bumpers are working a menu
  const bool left = ButtonDown(c, 4, 0x1A);   // left bumper, [
  const bool right = ButtonDown(c, 6, 0x1B);  // right bumper, ]
  if (left && !s.left) s.left_ms = 0;
  if (right && !s.right) s.right_ms = 0;
  if (left) s.left_ms += dt;
  if (right) s.right_ms += dt;
  if (left && right) s.both = true;
  const bool left_tap = !left && s.left && s.left_ms <= kTapMs;
  const bool right_tap = !right && s.right && s.right_ms <= kTapMs;
  s.left = left;
  s.right = right;
  if (s.both) {
    if (left || right) return;  // until both are up
    s.both = false;
    if (s.left_ms <= kTapMs && s.right_ms <= kTapMs) {
      DropCrash(s.car);
      SetHazard(veh, !(Rd8(veh + kLightFlags) & 2));
      s.side = 0;
      s.stopping = false;
    }
    return;
  }
  const int tap = left_tap ? 1 : right_tap ? 2 : 0;
  if (!tap) return;
  if (Rd8(veh + kLightFlags) & 2) {  // a side ends the hazard lights
    DropCrash(s.car);
    SetHazard(veh, false);
  }
  s.side = s.side == tap ? 0 : tap;
  s.stopping = false;
}

// Once a frame: the crashed vehicles' hazard lights and the player's
// vehicle's indicator.
void Update(Ctx& c) {
  const uint32_t clock = Rd32(kClock);
  float dt = g_clock_set ? float(int32_t(clock - g_clock)) : 0.0f;
  g_clock = clock;
  g_clock_set = true;
  if (dt < 0) dt = 0;
  if (dt > 250) dt = 250;  // a pause or a load

  // (Not as a passenger: a taxi's indicators are its driver's.)
  const uint32_t ped = PlayerPed();
  uint32_t veh = PlayerVehicle(ped);
  if (veh && Rd32(veh + 0xFA0) != ped) veh = 0;
  for (size_t i = 0; i < g_crashes.size();) {
    Crash& k = g_crashes[i];
    const uint32_t v = VehicleAt(k.car);
    if (!v) {
      g_crashes.erase(g_crashes.begin() + i);
      continue;
    }
    if (v != veh) {
      k.wait_exit = false;
    } else if (!k.wait_exit) {
      SetHazard(v, false);
      g_crashes.erase(g_crashes.begin() + i);
      continue;
    }
    i++;
  }

  Indicators& s = g_ind;
  if (!veh) {
    s = Indicators{};
    return;
  }
  const uint32_t h = Handle(veh);
  if (h != s.car) {
    s = Indicators{};
    s.car = h;
  }
  s.ptr = veh;
  if (ManualIndicators())
    Manual(c, veh, dt);
  else
    Automatic(c, veh, dt);
  if (s.side && s.stopping && !BlinkOn(veh, clock)) {
    s.side = 0;
    s.stopping = false;
  }
}

}  // namespace

// The vehicle light pass (008FB0B0, ebx the vehicle; run for each vehicle
// from its PreRender) has just set which side's indicators are lit
// ([esp+22] left, [esp+21] right: from the AI's turn flags) and reads the
// light flags next: mov cl, [ebx+F6C]. The player's vehicle's indicator
// side is lit with the game's blink phase (al), and only with the engine
// on, as the game lights the AI's turn flags.
void hook_008FC09A(Ctx& c) {
  c.ecx = (c.ecx & ~0xFFu) | Rd8(c.ebx + kLightFlags);
  if (!TurnIndicatorsOn()) return;
  int side = 0;
  {
    std::lock_guard<std::mutex> l(g_mu);
    const uint32_t frame = Rd32(kFrame);
    if (frame != g_frame) {
      g_frame = frame;
      Update(c);
    }
    if (g_ind.ptr == c.ebx) side = g_ind.side;
  }
  if (side && (c.eax & 1) && (Rd8(c.ebx + kEngineFlags) & 8)) Wr8(c.esp + (side == 1 ? 0x22 : 0x21), 1);
}

// A ped in a vehicle's crash is to be thrown through the windscreen
// (00AA0B40: esi the ped, edi its vehicle; the event type set here, then
// the check whether it can go): mov dword ptr [eax], 6. The vehicle's
// hazard lights go on when the ped really goes (the game's check after it,
// 004C5030: [ped+218] clear and [ped+219] set).
void hook_00AA0F75(Ctx& c) {
  Wr32(c.eax, 6);
  if (!TurnIndicatorsOn() || !c.edi) return;
  if (Rd8(c.esi + 0x218) || !Rd8(c.esi + 0x219)) return;
  std::lock_guard<std::mutex> l(g_mu);
  const uint32_t h = Handle(c.edi);
  if (h == kNone) return;
  const uint32_t me = PlayerPed();
  const bool player = c.esi == me || (me && PlayerVehicle(me) == c.edi);  // the player was in it
  SetHazard(c.edi, true);
  for (Crash& k : g_crashes)
    if (k.car == h) {
      k.wait_exit = k.wait_exit || player;
      return;
    }
  if (g_crashes.size() >= 8) {  // the oldest goes off
    if (const uint32_t v = VehicleAt(g_crashes.front().car)) SetHazard(v, false);
    g_crashes.erase(g_crashes.begin());
  }
  g_crashes.push_back({h, player});
}

// The places a vehicle without a driver has its front wheels set
// straight (or, a bike on its stand, turned by its lean): movss
// [veh+10D8], xmm0. Kept as they are for a vehicle that has been the
// player's while no one but the player drives it.
namespace {
void StoreSteer(uint32_t veh, const Xmm& x) {
  static const bool on = OptionOn("GTAIV_KEEP_WHEELS_TURNED", false);
  if (on && (Rd8(veh + kOwnFlags) & 2)) {
    const uint32_t driver = Rd32(veh + kDriver);
    if (!driver || driver == PlayerPed()) return;
  }
  WrF32(veh + kSteerAngle, Lane0(x));
}
}  // namespace

void hook_00A55361(Ctx& c) { StoreSteer(c.esi, c.xmm[0]); }  // CAutomobile::ProcessControl, no driver
void hook_00C65BDB(Ctx& c) { StoreSteer(c.esi, c.xmm[0]); }  // the autopilot's stop
void hook_00B9BDC4(Ctx& c) { StoreSteer(c.esi, c.xmm[0]); }  // CBike, no driver
void hook_00B9BE6F(Ctx& c) { StoreSteer(c.esi, c.xmm[0]); }  // CBike on its stand

}  // namespace recomp::gen
