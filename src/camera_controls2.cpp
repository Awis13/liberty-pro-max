// Camera and control options, second round: two from FusionFix
// (ThirteenAG and contributors, GPL-3.0; done again for 1.0.8.0 by their
// ideas, its code not taken) and one from ZolikaPatch's list (by its
// meaning only):
//   GTAIV_ALWAYS_RUN (off, as FusionFix): with the game's sprint setting
//     on "hold", Niko runs without the sprint key; a short tap (under
//     0.2 s) switches between running and jogging and does not sprint, a
//     longer press sprints; holding the key keeps sprinting (with the
//     "tap" setting too); sprinting is allowed indoors;
//   GTAIV_EXTENDED_SNIPER (off, as FusionFix): weapons with a scope (the
//     FIRST_PERSON flag of weaponinfo.xml) aim over the shoulder like the
//     others; Jump while aiming one puts the scope up, Jump again takes it
//     down (FusionFix's lighter recoil out of the scope is not part of it,
//     see wf2_camera2's notes);
//   GTAIV_DRIVEBY_FOV_FIX (on, as ZolikaPatch): the game camera's field of
//     view is not widened with height (camera above 50 m) while the player
//     does a drive-by, so the shots land in the middle of the crosshair.
//     This widening is the game's only height-dependent camera change
//     (its constants have no other reader), the one ZolikaPatch's "too
//     high above ground level" points at.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

namespace {

constexpr uint32_t kClockMs = 0x010FCB6C;  // the game's clock, ms
constexpr uint32_t kFrame = 0x010FCC30;    // frame counter
constexpr uint32_t kOne = 0x00D6D7C8;      // float 1.0 in .rdata

// FindPlayerPed (007CD230) read in place: the local player's info, its ped.
uint32_t PlayerPed() {
  const uint32_t idx = Rd32(0x00F2E0B4);
  if (idx == 0xFFFFFFFFu) return 0;
  const uint32_t info = Rd32(0x01100498 + 4 * idx);
  return info ? Rd32(info + 0x58C) : 0;
}

// The flags of test r, r (32-bit) in c.eflags.
void TestFlags32(Ctx& c, uint32_t r) {
  Flags f;
  UnpackEFlags(f, c.eflags);
  lift::Logic<4>(f, r);
  c.eflags = PackEFlags(f);
}

// A task's type: its virtual GetTaskType (vtable +0C, thiscall).
uint32_t TaskType(Ctx& c, uint32_t task) {
  KeepRegs keep(c);
  c.ecx = task;
  return uint32_t(CallGuest(c, Rd32(Rd32(task) + 0x0C), {}));
}

// IS_PED_DOING_DRIVEBY's own test (00B3F1E0, by handle) for the player:
// the active task (the first of the five priority slots at intelligence
// +44, followed down its subtasks at +08) is CTaskSimpleNewGangDriveBy
// (1049) in its states 3..5, or, for a player ped (+219), a task of
// type 733.
bool PlayerDoingDriveby(Ctx& c) {
  const uint32_t ped = PlayerPed();
  if (!ped) return false;
  const uint32_t intel = Rd32(ped + 0x224);
  if (!intel) return false;
  uint32_t task = 0;
  for (uint32_t i = 0; i < 5 && !task; i++) task = Rd32(intel + 0x44 + 4 * i);
  if (!task) return false;
  while (const uint32_t sub = Rd32(task + 0x08)) task = sub;
  const uint32_t type = TaskType(c, task);
  if (type == 0x419 && Rd32(task + 0x44) - 3 <= 2) return true;
  return Rd8(ped + 0x219) != 0 && type == 0x2DD;
}

}  // namespace

// ---- GTAIV_DRIVEBY_FOV_FIX ----------------------------------------------

// CCamGame::PostProcessHeightBasedFOV (00A777C0, called from CCamGame's
// update 00A78F30; replaced in options.cpp, which runs this lifted body):
// test eax, eax on the search for a CCamIdle (camera type 12, vtable
// 00DE32DC) under the game camera; one found (jne) leaves the field of view
// alone. A drive-by of the player's skips the widening the same way.
void hook_00A777CC(Ctx& c) {
  static const bool on = OptionOn("GTAIV_DRIVEBY_FOV_FIX", true);
  uint32_t v = c.eax;
  if (!v && on && PlayerDoingDriveby(c)) v = 1;
  TestFlags32(c, v);
}

// ---- GTAIV_ALWAYS_RUN ---------------------------------------------------

// The player's sprint (00998880, thiscall ped: x87 result, 0 jog, 1 run,
// above 1 sprint) reads the sprint control at pad +26A8 (+4 xor +6 down,
// +4 xor +7 down the frame before); mPrefs[PREF_SPRINT] ([010FC0B8] +
// 9D * 4, the index from the game's PREF_ name table) at 0 makes the
// keyboard's key a toggle (pad +328E), else it is held.
namespace {

constexpr uint32_t kPrefSprint = 0x010FC32C;
constexpr uint32_t kTapMs = 200;

bool AlwaysRun() {
  static const bool on = OptionOn("GTAIV_ALWAYS_RUN", false);
  return on;
}
bool SprintHeld() { return Rd32(kPrefSprint) != 0; }

struct Run {
  bool running = true;      // runs without the key; a tap switches it
  bool long_press = false;  // this press is past the tap time: sprinting
  uint32_t press_ms = 0;
  uint32_t switched_frame = 0xFFFFFFFFu;
} g_run;

}  // namespace

// Sprint as taps (the default model): a press adds to the sprint energy
// only on the frame it starts (cmp al, 7F on the frame before; ja). With
// the option each frame it is held does, so holding the key sprints.
void hook_009989A7(Ctx& c) { CmpFlags<1>(c, AlwaysRun() ? 0 : uint8_t(c.eax), 0x7F); }

// Energy over its threshold (comiss xmm0, 1.0; above: sprint). With the
// option and the held setting, a press shorter than the tap time does
// not sprint, and its release switches running and jogging (once a frame,
// the function runs for more than one task).
void hook_00998BB5(Ctx& c) {
  float ratio = Lane0(c.xmm[0]);
  if (AlwaysRun() && SprintHeld()) {
    const uint32_t io = c.ebp + 0x26A8;  // ebp: the ped's pad
    const uint8_t base = Rd8(io + 4);
    const bool down = uint8_t(Rd8(io + 6) ^ base) > 0x7F;
    const bool was = uint8_t(Rd8(io + 7) ^ base) > 0x7F;
    const uint32_t now = Rd32(kClockMs);
    if (down && !was) {
      g_run.press_ms = now;
      g_run.long_press = false;
    }
    if (down) {
      if (int32_t(now - g_run.press_ms) >= int32_t(kTapMs))
        g_run.long_press = true;
      else
        ratio = std::min(ratio, 1.0f);
    } else if (was && !g_run.long_press && Rd32(kFrame) != g_run.switched_frame) {
      g_run.switched_frame = Rd32(kFrame);
      g_run.running = !g_run.running;
    }
    SetLane0(c.xmm[0], ratio);
  }
  ComissFlags(c, ratio, RdF32(kOne));
}

// Not sprinting: cmp cl, 7F on the key (jbe: not down, the result capped
// at 1). With the option and the held setting, up, the result is 1: run.
void hook_00998C73(Ctx& c) {
  const uint8_t v = uint8_t(c.ecx);
  if (v <= 0x7F && AlwaysRun() && SprintHeld() && g_run.running) WrF32(c.esp + 0x1C, 1.0f);
  CmpFlags<1>(c, v, 0x7F);
}

// CTaskSimpleMovePlayer's "no sprinting in interiors" ([00F4AE72], 1, only
// read): cmp byte [00F4AE72], 0 in the sprint and in two network-game
// paths; 0 with the option.
namespace {
uint8_t NoSprintIndoors() { return AlwaysRun() ? 0 : Rd8(0x00F4AE72); }
}  // namespace
void hook_00998938(Ctx& c) { CmpFlags<1>(c, NoSprintIndoors(), 0); }
void hook_00A5C14B(Ctx& c) { CmpFlags<1>(c, NoSprintIndoors(), 0); }
void hook_00C0E758(Ctx& c) { CmpFlags<1>(c, NoSprintIndoors(), 0); }

// ---- GTAIV_EXTENDED_SNIPER ----------------------------------------------

// A weapon's info is 0124A600 + type * 110 (CWeapon::GetInfo, 008DDEB0);
// its flags at +20, bit 3 FIRST_PERSON (the flag names' table at 00F3EEB0:
// CAN_AIM, CAN_AIM_WITH_ARM, CAN_FREE_AIM, FIRST_PERSON, ...), the scope.
// The game reads that bit to pick the scope camera (00A77180, 00A77286)
// and its reticle (004A8DCA); with the option it is taken away there
// unless the player asked for the scope.
namespace {

constexpr uint32_t kTaskAimGun = 0x411; // CTaskSimpleAimGun (1041)

bool ExtendedSniper() {
  static const bool on = OptionOn("GTAIV_EXTENDED_SNIPER", false);
  return on;
}

struct Sniper {
  bool want_scope = false;
  bool cancel = false;          // scope taken down: no aiming till it is gone
  bool cancel_started = false;
} g_sniper;

// A live scope camera (type 27): the camera manager's own lookup
// (009DF560, thiscall on 00F488E8; finished cameras skipped), as the
// reticle asks it (004A8E31).
bool InScope(Ctx& c) {
  KeepRegs keep(c);
  c.ecx = 0x00F488E8;
  return CallGuest(c, 0x009DF560, {}) != 0;
}

// CPedWeapons' current weapon (009CBC30, thiscall on ped +2B0) read in
// place, and its info.
uint32_t CurrentWeaponInfo(uint32_t ped) {
  const uint32_t w = ped + 0x2B0;
  uint32_t weapon = 0;
  if (const uint32_t obj = Rd32(w + 0x2C)) {
    const uint32_t cw = Rd32(obj + 0x25C);
    if (cw && Rd32(cw + 0x18) == Rd32(w + 4 * ((Rd32(w + 0x18) + 5) * 3))) weapon = cw;
  } else {
    const uint32_t slot = Rd32(w + 0x18);
    if (slot == 0 || slot == 0x0A) weapon = Rd32(w + 0x38);
  }
  const uint32_t type = weapon ? Rd32(weapon + 0x18) : 0;
  return 0x0124A600 + (int32_t(type) < 0x3C ? type * 0x110 : 0);
}

// A CTaskSimpleAimGun in the ped's task list (intelligence +2E0: node +4
// type, +C next; as the game's own task natives walk it, 00B3F270).
bool Aiming(uint32_t ped) {
  const uint32_t intel = Rd32(ped + 0x224);
  if (!intel) return false;
  for (uint32_t n = Rd32(intel + 0x2E0); n; n = Rd32(n + 0x0C))
    if (Rd32(n + 4) == kTaskAimGun) return true;
  return false;
}

// IS_CONTROL_JUST_PRESSED (00B4D060, cdecl: pad 0, control 2 = jump).
bool JumpJustPressed(Ctx& c) {
  KeepRegs keep(c);
  return uint8_t(CallGuest(c, 0x00B4D060, {0, 2})) != 0;
}

uint32_t SniperFlags(uint32_t flags) {
  if (ExtendedSniper() && (flags & 8) && (!g_sniper.want_scope || g_sniper.cancel_started)) {
    flags &= ~8u;
    if (!g_sniper.want_scope) g_sniper.cancel_started = false;
  }
  return flags;
}

// The player wants to aim (00A60230, cdecl char(char), its argument
// pushed). Jump while aiming a scoped weapon puts the scope up or, in it,
// takes it down; taken down, aiming stops till the scope camera is gone.
void WantsToAim(Ctx& c) {
  bool block = false;
  if (ExtendedSniper()) {
    const bool in_scope = InScope(c);
    if (JumpJustPressed(c)) {
      const uint32_t ped = PlayerPed();
      if (ped && (Rd32(CurrentWeaponInfo(ped) + 0x20) & 8) && Aiming(ped)) {
        if (in_scope) {
          g_sniper.want_scope = false;
          g_sniper.cancel = true;
          g_sniper.cancel_started = true;
        } else {
          g_sniper.want_scope = true;
        }
      }
    }
    if (g_sniper.cancel && in_scope)
      block = true;
    else if (!in_scope)
      g_sniper.cancel = false;
  }
  if (block)
    c.eax = 0;
  else
    CallGuest(c, 0x00A60230, {});
}

}  // namespace

// The camera factory (009DF770, thiscall: type, ...): mov eax, [esp+0C],
// the type, nothing else (the scope is asked of the camera manager,
// InScope: no camera is made when the scope goes and the follow camera
// is kept).
void hook_009DF78E(Ctx& c) { c.eax = Rd32(c.esp + 0x0C); }

// The weapon's flags, read for the scope: mov eax, [eax+20] / mov eax,
// [edx+20] (the aiming camera), mov ecx, [ebx+20] (the reticle).
void hook_00A77180(Ctx& c) { c.eax = SniperFlags(Rd32(c.eax + 0x20)); }
void hook_00A77286(Ctx& c) { c.eax = SniperFlags(Rd32(c.edx + 0x20)); }
void hook_004A8DCA(Ctx& c) { c.ecx = SniperFlags(Rd32(c.ebx + 0x20)); }

// The two calls of the player's "wants to aim" in the on-foot aiming
// decision (inside 00A60260).
void hook_00A60463(Ctx& c) { WantsToAim(c); }
void hook_00A607AC(Ctx& c) { WantsToAim(c); }

}  // namespace recomp::gen
