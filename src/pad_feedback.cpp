// A DualSense's adaptive triggers and light bar (GTAIV_PAD_FEEDBACK, off
// by default): once a frame the game is asked, through its own script
// commands (natives), what the player is doing; pad_feedback.h says what
// the triggers and the light do then, and the runtime sends that to the
// first pad when it is a DualSense and it is not what the pad has
// (SetPadEffects). Other pads get nothing; with the option off nothing is
// read and nothing sent. The game's own rumble is not touched: it reaches
// the pad as on any other (XInputSetState).
//
//   GTAIV_PAD_FEEDBACK_LOG=1  logs what was read from the game, each time
//                             it changes.

#include "pad_feedback.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "hook_util.h"
#include "option.h"
#include "runtime/hle/xinput.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"
#include "script_natives.h"

namespace recomp::gen {

using namespace hook;

namespace {

using natives::kOut;

enum Command : uint32_t {
  GET_PLAYER_ID = 0x62E319C6,
  CONVERT_INT_TO_PLAYERINDEX = 0x5996315E,
  GET_PLAYER_CHAR = 0x511454A9,
  IS_PLAYER_PLAYING = 0x08274BA4,
  IS_PLAYER_CONTROL_ON = 0x30CD2F1F,
  IS_PAUSE_MENU_ACTIVE = 0x6C4568A7,
  STORE_WANTED_LEVEL = 0x12AA6D71,
  IS_CHAR_IN_ANY_CAR = 0x71184DA3,
  IS_CHAR_IN_ANY_HELI = 0x0FC40275,
  GET_CAR_CHAR_IS_USING = 0x1B067237,
  GET_DRIVER_OF_CAR = 0x22457083,
  IS_VEH_DRIVEABLE = 0x17BC668D,
  GET_VEHICLE_GEAR = 0x2D2F452D,
  GET_CAR_SPEED = 0x16DD2D00,
  GET_CURRENT_CHAR_WEAPON = 0x5AB8289F,
  GET_WEAPONTYPE_SLOT = 0x5E4F6DE3,
  // The ped's "fired a shot" flag, which the game's weapon firing sets
  // (CPed +29C bit 08000000, set by 009D0B80 as a round is fired).
  IS_CHAR_SHOOTING = 0x324D1594,
  // The rounds in the clip of the ped's current weapon (false for another).
  GET_AMMO_IN_CLIP = 0x612C748F,
};

// A command's handler, looked up once (these run every frame).
gaddr Handler(Ctx& c, Command command) {
  struct Known {
    uint32_t hash;
    gaddr handler;
  };
  static Known known[16];
  static int count = 0;
  for (int i = 0; i < count; i++)
    if (known[i].hash == command) return known[i].handler;
  const gaddr handler = natives::Find(c, command);
  if (handler && count < 16) known[count++] = {command, handler};
  return handler;
}

// Calls a command with 4-byte arguments; returns its first result.
uint32_t Native(Ctx& c, uint32_t base, Command command, std::initializer_list<uint32_t> args) {
  const gaddr handler = Handler(c, command);
  return handler ? natives::Call(c, base, handler, args) : 0;
}

bool Flag(Ctx& c, uint32_t base, Command command, std::initializer_list<uint32_t> args) {
  return (Native(c, base, command, args) & 0xFF) != 0;
}

// A command that answers through a pointer: what it wrote there, 0 if nothing.
uint32_t Out(Ctx& c, uint32_t base, Command command, uint32_t arg) {
  Wr32(base + kOut, 0);
  Native(c, base, command, {arg, base + kOut});
  return Rd32(base + kOut);
}

bool On() {
  static const bool on = OptionOn("GTAIV_PAD_FEEDBACK", false);
  return on;
}

// What the game says the player is doing, and the weapon slot behind
// State::weapon (for the log).
pad_feedback::State Read(Ctx& c, int* slot) {
  pad_feedback::State s;
  *slot = 0;
  KeepRegs keep(c);
  const uint32_t esp = c.esp;  // (KeepRegs leaves it)
  if (CallGuest(c, natives::kFindPlayerPed, {})) {
    c.esp = (esp - natives::kSize) & ~0xFu;
    const uint32_t base = c.esp;
    const uint32_t player = Native(c, base, CONVERT_INT_TO_PLAYERINDEX, {Native(c, base, GET_PLAYER_ID, {})});
    const uint32_t ped = Out(c, base, GET_PLAYER_CHAR, player);
    s.playing = ped && Flag(c, base, IS_PLAYER_PLAYING, {player}) && Flag(c, base, IS_PLAYER_CONTROL_ON, {player}) &&
                !Flag(c, base, IS_PAUSE_MENU_ACTIVE, {}) && !uint8_t(CallGuest(c, natives::kCutsceneRunning, {}));
    if (s.playing) {
      s.wanted = int(Out(c, base, STORE_WANTED_LEVEL, player));
      if (Flag(c, base, IS_CHAR_IN_ANY_CAR, {ped})) {
        // Driving: at the wheel (not a passenger), not a helicopter (its
        // triggers are not pedals), and the vehicle still goes.
        const uint32_t car = Out(c, base, GET_CAR_CHAR_IS_USING, ped);
        if (car && Out(c, base, GET_DRIVER_OF_CAR, car) == ped && !Flag(c, base, IS_CHAR_IN_ANY_HELI, {ped}) &&
            Flag(c, base, IS_VEH_DRIVEABLE, {car})) {
          s.driving = true;
          s.gear = int(Native(c, base, GET_VEHICLE_GEAR, {car}));
          const uint32_t speed = Out(c, base, GET_CAR_SPEED, car);
          std::memcpy(&s.speed, &speed, 4);
        }
      } else {
        const uint32_t type = Out(c, base, GET_CURRENT_CHAR_WEAPON, ped);
        *slot = int(Out(c, base, GET_WEAPONTYPE_SLOT, type));
        s.weapon = pad_feedback::WeaponOfSlot(*slot);
        if (s.weapon != pad_feedback::Weapon::kNone) {
          s.weapon_type = int(type);
          s.shooting = Flag(c, base, IS_CHAR_SHOOTING, {ped});
          Wr32(base + kOut, 0);
          if (Flag(c, base, GET_AMMO_IN_CLIP, {ped, type, base + kOut})) s.clip = int(Rd32(base + kOut));
        }
      }
    }
  }
  c.esp = esp;
  s.ms = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
  return s;
}

void Log(const pad_feedback::State& s, int slot, pad_feedback::Fire fire) {
  static const bool on = OptionOn("GTAIV_PAD_FEEDBACK_LOG", false);
  if (!on) return;
  static pad_feedback::State last;
  static int last_slot = -1;
  static pad_feedback::Fire last_fire = pad_feedback::Fire::kNone;
  if (s.playing == last.playing && s.driving == last.driving && s.gear == last.gear && s.weapon == last.weapon &&
      s.wanted == last.wanted && slot == last_slot && fire == last_fire)
    return;
  last = s;
  last_slot = slot;
  last_fire = fire;
  if (!s.playing) WarnF("gtaiv: pad feedback: not playing\n");
  else if (s.driving) WarnF("gtaiv: pad feedback: driving, gear %d, %.0f m/s, wanted %d\n", s.gear, s.speed, s.wanted);
  else
    WarnF("gtaiv: pad feedback: on foot, weapon slot %d (%s), fire %s (clip %d, shooting %d), wanted %d\n", slot,
          pad_feedback::WeaponName(s.weapon), pad_feedback::FireName(fire), s.clip, int(s.shooting), s.wanted);
}

}  // namespace

// Once a frame, from the main loop.
void GtaivPadFeedbackFrame(Ctx& c) {
  static pad_feedback::PadFeedbackLoop loop;
  pad_feedback::State read;
  int slot = 0;
  bool was_read = false;
  loop.Frame(
      On(),
      [&] {
        read = Read(c, &slot);
        was_read = true;
        return read;
      },
      [](const recomp::hle::PadEffects& effects) { recomp::hle::SetPadEffects(0, effects); });
  if (was_read) Log(read, slot, loop.feedback().fire());
}

// As the game exits: the pad keeps its last effects otherwise.
void GtaivPadFeedbackExit() {
  if (On()) recomp::hle::ClearPadEffects();
}

}  // namespace recomp::gen
