// Vehicle fixes and additions, done again for 1.0.8.0 by the community's
// ideas (FusionFix's turn indicators, GPL-3.0; its code not taken). Each
// one is an option (the launcher's Advanced tab sets them), off unless
// said otherwise:
//   GTAIV_TURN_INDICATORS (off by default): the player's vehicle shows its
//     indicators while it turns (the wheel held well over for a moment),
//     blinking as the game's own hazard lights do; a crash hard enough to
//     throw someone through the windscreen puts the hazard lights on until
//     the player next gets into that vehicle (after leaving it, if he was
//     the one in it);
//   GTAIV_TURN_INDICATORS_MANUAL (off by default, with the above): the
//     indicators by hand instead: a tap of [ or ] (or of the pad's left or
//     right bumper) switches the left or right one on or off, a tap of
//     both together the hazard lights;
//   GTAIV_KEEP_WHEELS_TURNED (off by default): a vehicle the player leaves
//     keeps its front wheels (or handlebars) turned as he left them, not
//     straightened.

#include <cstdint>
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

// How far over the wheel must be (a share of the full lock) and for how
// long before the indicator comes on; a tap is a press this short.
constexpr float kTurnShare = 0.6f;
constexpr float kTurnHoldMs = 400.0f;
constexpr float kTapMs = 300.0f;

// The player's vehicle's indicator (0 none, 1 left, 2 right) and what
// decides it.
struct Indicators {
  uint32_t car = kNone;   // the vehicle's handle
  uint32_t ptr = 0;       // and the vehicle this frame
  int side = 0;
  bool stopping = false;  // off once this blink is over
  int turn = 0;           // automatic: the way the wheel is over
  float held = 0;         //   for this long (ms)
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

void Automatic(uint32_t veh, float dt) {
  Indicators& s = g_ind;
  const float lock = RdF32(Rd32(veh + kHandling) + 0x84);
  const float angle = RdF32(veh + kSteerAngle);
  int turn = 0;  // the angle is positive to the left
  if (lock > 0) {
    if (angle > kTurnShare * lock) turn = 1;
    else if (angle < -kTurnShare * lock) turn = 2;
  }
  if (turn != s.turn) {
    s.turn = turn;
    s.held = 0;
  } else if (turn) {
    s.held += dt;
  }
  if (s.side && s.side == turn) s.stopping = false;
  if (turn && s.side != turn && s.held >= kTurnHoldMs) {
    s.side = turn;
    s.stopping = false;
  }
  if (s.side && s.side != turn) s.stopping = true;  // straightened or the other way
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
    Automatic(veh, dt);
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
