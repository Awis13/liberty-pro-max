// Vehicle fixes and additions, done again for 1.0.8.0 by the community's
// ideas (FusionFix's turn indicators, GPL-3.0; its code not taken). Each
// one is an option (the launcher's Advanced tab sets them), off unless
// said otherwise:
//   GTAIV_TURN_INDICATORS (off by default): the player's vehicle shows its
//     indicators through a real turn: on once the wheel is over and the
//     vehicle has turned that way, or the wheel at full lock for a moment;
//     off as a car's stalk cancels, when the wheel has come back. Both only
//     near a junction, by the game's road nodes. A quick
//     flick of the pad's stick all the way over sets it before the turn, as
//     a stalk does: on through the turn, or for 12 s without one, or until
//     a flick the same way; blinking
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
constexpr float kTurnStartDeg = 12.0f;
constexpr float kTurnBackDeg = 35.0f;
constexpr float kTurnBackWindowMs = 3000.0f;
constexpr float kTurnMinSpeed = 1.0f;
constexpr float kTurnMinRate = 10.0f;
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
constexpr float kTurnLatchMs = 12000.0f;
// A turn counts near a junction only: among the closest road nodes, one not
// far behind and within this distance (m) whose road crosses the vehicle's
// way by at least this much (degrees; a curving street's nodes stay nearer).
// Asked only when a turn would start, and not more often than this.
constexpr uint32_t kFindNative = 0x00626AF0;  // esi: the hash; returns the handler
constexpr uint32_t kNthClosestCarNodeWithHeading = 0x1F6B3FF0;
constexpr int kJunctionNodes = 8;
constexpr float kJunctionRange = 30.0f;
constexpr float kJunctionBehind = 10.0f;
constexpr float kJunctionCross = 50.0f;
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
  bool left = false, right = false;  // manual: what is down
  float left_ms = 0, right_ms = 0;   //   since when
  bool both = false;                 //   both were down together
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
int Flick(Ctx& c, float dt) {
  Indicators& s = g_ind;
  int raw = 0;
  {
    KeepRegs keep(c);
    if (uint8_t(CallGuest(c, 0x00B4D3D0, {}))) raw = int32_t(CallGuest(c, 0x00B4CD40, {0, 0}));
  }
  int out = 0;
  if (std::abs(raw) < kFlickRest) {
    if (s.flick_dir) {
      if (s.flick_dir != 3 && s.flick_full && s.flick_ms <= kFlickMs) out = s.flick_dir;
      if (TurnLog()) std::fprintf(stderr, "turn: stick back, dir %d full %d %.0f ms -> %d\n", s.flick_dir, s.flick_full, s.flick_ms, out);
      s.flick_dir = 0;
      s.flick_full = false;
      s.rest_ms = 0;
    }
    s.rest_ms += dt;
    return out;
  }
  const int dir = raw < 0 ? 1 : 2;
  if (!s.flick_dir) {
    s.flick_dir = s.rest_ms >= kFlickRestMs ? dir : 3;
    s.flick_ms = 0;
  } else if (s.flick_dir != dir) {
    s.flick_dir = 3;
  }
  s.flick_ms += dt;
  if (std::abs(raw) >= kFlickFull) s.flick_full = true;
  return 0;
}

// Whether a road crosses near the vehicle (at x, y, z, forward fx, fy): the
// scripts' GET_NTH_CLOSEST_CAR_NODE_WITH_HEADING (x, y, z, n, out x, y, z,
// heading), called as a script calls it. No nodes at all (none loaded) counts
// as yes, so as not to lose the indicator.
bool JunctionNear(Ctx& c, float x, float y, float z, float fx, float fy) {
  KeepRegs keep(c);
  const auto bits = [](float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
  };
  c.esi = kNthClosestCarNodeWithHeading;
  const gaddr fn = gaddr(CallGuest(c, kFindNative, {}));
  if (!fn) return true;
  const uint32_t stack = uint32_t(c.esp);
  const uint32_t base = (stack - 0x100) & ~0xFu;  // below the caller's; calls go below it
  c.esp = base;
  const uint32_t ctx = base, args = base + 0x60, out = base + 0xA0;
  const float way = std::atan2(-fx, fy) * 180 / kPi;  // as the game's headings
  bool any = false, found = false;
  char line[256];
  int used = std::snprintf(line, sizeof line, "turn: nodes");
  for (int n = 1; n <= kJunctionNodes; n++) {
    std::memset(GPtr(base), 0, 0xC0);
    Wr32(ctx + 0x00, args);
    Wr32(ctx + 0x04, 8);
    Wr32(ctx + 0x08, args);
    const uint32_t a[8] = {bits(x), bits(y), bits(z), uint32_t(n), out, out + 4, out + 8, out + 12};
    for (int i = 0; i < 8; i++) Wr32(args + 4 * i, a[i]);
    CallGuest(c, fn, {ctx});
    const float dx = RdF32(out) - x, dy = RdF32(out + 4) - y, heading = RdF32(out + 12);
    if (dx == -x && dy == -y) continue;  // no such node
    any = true;
    const float dist = std::hypot(dx, dy), along = dx * fx + dy * fy;
    float cross = std::fmod(std::fabs(heading - way), 180.0f);
    if (cross > 90) cross = 180 - cross;
    if (dist <= kJunctionRange && along >= -kJunctionBehind && cross >= kJunctionCross) found = true;
    if (TurnLog() && used < 220) used += std::snprintf(line + used, sizeof line - used, " %.0fm/%.0f", dist, cross);
  }
  c.esp = stack;
  if (TurnLog()) std::fprintf(stderr, "%s -> %d%s\n", line, found, any ? "" : " (none)");
  return found || !any;
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
  s.full_ms = turn && std::fabs(share) >= kTurnFullShare && !reversing ? s.full_ms + dt : 0;
  if (flick && s.side == flick) {  // the same way again: off
    s.latched = s.real_turn = false;
    s.on_ms = kTurnMinOnMs;
    s.back_ms = kTurnBackMs;
  } else if (flick && (s.latched || (!s.side && !(s.real_turn && s.last_ms < kTurnBackWindowMs)))) {
    // (Not while a turn's indicator is on or just over: the wheel being
    // straightened can look like a flick.)
    s.side = flick;
    s.latched = true;
    s.latch_ms = s.on_ms = s.back_ms = 0;
    s.real_turn = false;
  }
  const bool back = s.last_side && turn != s.last_side && s.last_ms < kTurnBackWindowMs;
  bool turning = turn && (s.turned >= (back ? kTurnBackDeg : kTurnStartDeg) ||
                          s.full_ms >= (back ? kTurnFullBackMs : kTurnFullMs));
  if (turning && turn != s.side && mat) {
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
  s.stopping = s.side && !s.latched && s.on_ms >= kTurnMinOnMs && s.back_ms >= kTurnBackMs;

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

  const uint32_t veh = PlayerVehicle(PlayerPed());
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
// hazard lights go on.
void hook_00AA0F75(Ctx& c) {
  Wr32(c.eax, 6);
  if (!TurnIndicatorsOn() || !c.edi) return;
  std::lock_guard<std::mutex> l(g_mu);
  const uint32_t h = Handle(c.edi);
  if (h == kNone) return;
  const bool player = c.esi == PlayerPed();
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
