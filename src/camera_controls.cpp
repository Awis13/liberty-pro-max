// Camera and controls options from FusionFix (ThirteenAG and contributors,
// GPL-3.0; done again for 1.0.8.0 by its ideas, its code not taken). Off
// by default unless said otherwise, as FusionFix has them:
//   GTAIV_PAD_CAMERA_DELAY: on foot with a gamepad the camera does not
//     swing back behind Niko for 2 s after the right stick moved it (the
//     mouse's own delay is GTAIV_MOUSE_CAMERA_DELAY);
//   GTAIV_CAR_CAMERA_DELAY: in vehicles the camera does not swing back
//     behind the car for 2 s after the mouse or the right stick moved it;
//   GTAIV_AIM_ZOOM_MEMORY: the aiming zoom is remembered from one aim to
//     the next in every episode, as on the Xbox (on the PC only The Ballad
//     of Gay Tony reads it back, and starts every aim zoomed in; that
//     episode remembers it right even with the option off);
//   GTAIV_HELI_PAD_RETICLE (on): the first-person aiming reticle of armed
//     helicopters (Annihilator, Buzzard) with a gamepad too, not only with
//     keyboard and mouse;
//   GTAIV_AUTO_LADDERS: Niko climbs a ladder when walking into it (within
//     1.5 m; the button still reaches 4 m), not again for 2.5 s after
//     getting off one;
//   GTAIV_WHEEL_STUNT_CAMERA (on): with keyboard and mouse the wheel sets
//     the slow motion of unique stunt jumps (as in GTA V), and does not
//     change the radio station meanwhile.

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

constexpr uint32_t kClockMs = 0x010FCB6C;   // the game's clock, ms
constexpr uint32_t kFrame = 0x010FCC30;     // frame counter
constexpr uint32_t kEpisode = 0x0112D754;   // 0 IV, 2 TBoGT

// IS_USING_CONTROLLER's own test (00B4D3D0, no arguments; al).
bool UsingPad(Ctx& c) {
  KeepRegs keep(c);
  return uint8_t(CallGuest(c, 0x00B4D3D0, {})) != 0;
}

// GET_PAD_STATE's lookup (00B4CD40, cdecl: pad, control): controls 2 and
// 3 are the right stick's axes, 0 when it rests.
bool RightStickMoved(Ctx& c) {
  KeepRegs keep(c);
  return CallGuest(c, 0x00B4CD40, {0, 2}) != 0 || CallGuest(c, 0x00B4CD40, {0, 3}) != 0;
}

// The mouse's deltas this frame (as GTAIV_MOUSE_CAMERA_DELAY reads them).
bool MouseMoved() { return Rd32(0x017945D0) || Rd32(0x017945C0); }

// Held for ms of game time after the last input; a clock going back (a
// save loaded) forgets it.
struct Delay {
  uint32_t until = 0, last = 0;
  bool Hold(uint32_t moved_ms) {
    const uint32_t now = Rd32(kClockMs);
    if (now < last) until = 0;
    last = now;
    if (moved_ms) until = now + moved_ms;
    return now < until;
  }
};

constexpr uint32_t kCameraDelayMs = 2000;

// An input's value (ioValue: +4 and +6 xored) is down past its middle.
bool Down(uint32_t io) { return uint8_t(Rd8(io + 6) ^ Rd8(io + 4)) > 0x7F; }

}  // namespace

// CCamFollowPed: how strongly the camera swings back behind the player
// this frame, (target - current heading) / time (divss xmm0, xmm1; stored
// next by the mouse delay's hook). With a gamepad held at 0 for 2 s after
// the right stick moved.
void hook_00A8A6DA(Ctx& c) {
  static const bool on = OptionOn("GTAIV_PAD_CAMERA_DELAY", false);
  static Delay delay;
  float v = Lane0(c.xmm[0]) / Lane0(c.xmm[1]);
  if (on && UsingPad(c) && delay.Hold(RightStickMoved(c) ? kCameraDelayMs : 0)) v = 0.0f;
  SetLane0(c.xmm[0], v);
}

// CCamFollowVehicle: the same across and up, (target - current) before the
// division by a time (subss; the turn speed's hooks divide next). Held at 0
// for 2 s after the right stick (with a gamepad) or the mouse moved.
namespace {
bool CarCameraHold(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CAR_CAMERA_DELAY", false);
  static Delay delay;
  if (!on) return false;
  const bool moved = UsingPad(c) ? RightStickMoved(c) : MouseMoved();
  return delay.Hold(moved ? kCameraDelayMs : 0);
}
}  // namespace
void hook_00A813F1(Ctx& c) {  // subss xmm0, [esi+194]
  const float v = Lane0(c.xmm[0]) - RdF32(c.esi + 0x194);
  SetLane0(c.xmm[0], CarCameraHold(c) ? 0.0f : v);
}
void hook_00A81104(Ctx& c) {  // subss xmm0, xmm1
  const float v = Lane0(c.xmm[0]) - Lane0(c.xmm[1]);
  SetLane0(c.xmm[0], CarCameraHold(c) ? 0.0f : v);
}

// The aiming camera's zoom is bit 0 of its byte +200. Starting an aim, The
// Ballad of Gay Tony takes it from [00F4C8C1] (cmp [episode], 2; jne),
// the others clear it; nothing on the PC writes that byte but a reset to 1
// (every aim zoomed in). In that episode, and in all with the option: the
// reset is to 0, and wherever the zoom changes (mouse wheel in and out,
// the pad's toggle) the byte follows it; with the option IV takes it too.
namespace {
bool AimZoomMemory() {
  static const bool on = OptionOn("GTAIV_AIM_ZOOM_MEMORY", false);
  return on;
}
bool AimZoomFollowed() { return AimZoomMemory() || Rd32(kEpisode) == 2; }
void SetAimZoom(Ctx& c, uint8_t v) {
  Wr8(c.esi + 0x200, v);
  if (AimZoomFollowed()) Wr8(0x00F4C8C1, v & 1);
}
}  // namespace
void hook_00A9B948(Ctx& c) { CmpFlags<4>(c, AimZoomMemory() ? 2 : Rd32(kEpisode), 2); }
void hook_009DD486(Ctx& c) { Wr8(0x00F4C8C1, AimZoomFollowed() ? 0 : uint8_t(c.ebx)); }  // mov [], bl
void hook_00A98F9D(Ctx& c) {  // or byte [esi+200], bl
  const uint8_t v = Rd8(c.esi + 0x200) | uint8_t(c.ebx);
  SetAimZoom(c, v);
  TestFlags8(c, v);
}
void hook_00A99047(Ctx& c) {  // and byte [esi+200], FE
  const uint8_t v = Rd8(c.esi + 0x200) & 0xFE;
  SetAimZoom(c, v);
  TestFlags8(c, v);
}
void hook_00A9906F(Ctx& c) { SetAimZoom(c, uint8_t(c.ecx)); }  // mov [esi+200], cl

// CCamFollowVehicle: the first-person reticle of an armed helicopter is
// drawn with keyboard and mouse only (cmp byte [pad+328D], 0; je: a
// gamepad, skip it). With the option (on) for a gamepad too; as with the
// mouse, the camera button then no longer reaches the cinematic camera there.
void hook_00A80538(Ctx& c) {
  static const bool on = OptionOn("GTAIV_HELI_PAD_RETICLE", true);
  CmpFlags<1>(c, on ? 1 : Rd8(c.ecx + 0x328D), 0);
}

// Ladders without the button. On foot (CTaskComplexPlayerOnFoot's input)
// the ladder scan (C77830, cdecl: ped, out, flag) runs when the climb
// button was just pressed (cmp cl, 7F: down; cmp dl, 7F: not down the
// frame before) or the ped's flags +270 ask for it; in water while the
// button is down (cmp al, 7F). With the option it runs every frame; a
// scan nothing asked for finds ladders within 1.5 m, not 4 (the
// callback's distance check, movss xmm4, [4.0]), and none for 2.5 s after
// the player got off one (CTaskSimpleClimbLadder's clean-up, C78090).
namespace {
bool AutoLadders() {
  static const bool on = OptionOn("GTAIV_AUTO_LADDERS", false);
  return on;
}
constexpr float kAutoLadderRange = 1.5f;
constexpr uint32_t kLadderRegrabMs = 2500;
bool g_ladder_down;    // on foot: the button down this frame
bool g_ladder_auto;    // on foot: this scan only from the option
bool g_water_auto;     // in water: the same
bool g_ladder_short;   // a scan with the short range is running
bool g_ladder_left_seen;
uint32_t g_ladder_left;  // when the player got off a ladder

uint32_t LadderScan(Ctx& c, bool automatic) {  // the arguments are pushed
  if (automatic) {
    const uint32_t now = Rd32(kClockMs);
    if (g_ladder_left_seen && now >= g_ladder_left && now - g_ladder_left < kLadderRegrabMs) return 0;
    g_ladder_short = true;
  }
  const uint32_t r = uint32_t(CallGuest(c, 0x00C77830, {}));
  g_ladder_short = false;
  return r;
}
}  // namespace
void hook_00A60CC6(Ctx& c) {  // cmp cl, 7F; jbe: not down
  const uint8_t v = uint8_t(c.ecx);
  g_ladder_down = v > 0x7F;
  CmpFlags<1>(c, AutoLadders() ? 0x80 : v, 0x7F);
}
void hook_00A60CD3(Ctx& c) {  // cmp dl, 7F; jbe: just pressed, scan
  const uint8_t v = uint8_t(c.edx);
  if (!AutoLadders()) {
    CmpFlags<1>(c, v, 0x7F);
    return;
  }
  const bool pressed = g_ladder_down && v <= 0x7F;
  g_ladder_auto = !pressed && !(Rd32(c.ebx + 0x270) & 0x80020000);
  CmpFlags<1>(c, 0, 0x7F);
}
void hook_00A60D28(Ctx& c) {  // call ScanForLadderToClimb
  c.eax = LadderScan(c, g_ladder_auto);
  g_ladder_auto = false;
}
void hook_00B686B6(Ctx& c) {  // cmp al, 7F; jbe: not down
  const uint8_t v = uint8_t(c.eax);
  g_water_auto = AutoLadders() && v <= 0x7F;
  CmpFlags<1>(c, g_water_auto ? 0x80 : v, 0x7F);
}
void hook_00B686DF(Ctx& c) {  // call ScanForLadderToClimb
  c.eax = LadderScan(c, g_water_auto);
  g_water_auto = false;
}
void hook_00C76B75(Ctx& c) {  // movss xmm4, [4.0]
  LoadSs(c.xmm[4], g_ladder_short ? kAutoLadderRange : RdF32(0x00DE42C4));
}
void hook_00C78091(Ctx& c) {  // mov esi, [esp+8]: the ped
  c.esi = Rd32(c.esp + 8);
  if (!AutoLadders()) return;
  const uint32_t ped = c.esi;
  bool player;
  {
    KeepRegs keep(c);  // no call here in the game's code
    player = ped && ped == uint32_t(CallGuest(c, 0x007CD230, {}));  // FindPlayerPed
  }
  if (player) {
    g_ladder_left = Rd32(kClockMs);
    g_ladder_left_seen = true;
  }
}

// Unique stunt jumps (the intermezzo's process): the slow motion follows
// a pad axis, or the mouse's vertical input with keyboard and mouse, as a
// value 0..255 around 128 (subss xmm0, [128.0]; above slower, below
// faster). With the option (on) and keyboard and mouse: a value kept for the
// jump instead, from 128, a quarter of each half per wheel notch (the
// wheel's zoom-out input slower, zoom-in faster, the inputs at +2C78 and
// +2C68 of the keyboard's pad, 86A1A0(1)). Meanwhile the radio's next and
// previous station inputs (the pad's +29D8, +29E8, just before they are
// read) are let go, as the wheel is theirs too; and the jump's first help
// text is FusionFix's for the mouse (USJ_FRST_KMB), when it is loaded.
namespace {
bool StuntWheel() {
  static const bool on = OptionOn("GTAIV_WHEEL_STUNT_CAMERA", true);
  return on;
}
bool g_stunt_seen, g_wheel_seen;
uint32_t g_stunt_frame, g_wheel_frame;
float g_stunt_value = 128.0f;

bool WheelSteering() {
  return g_wheel_seen && Rd32(kFrame) - g_wheel_frame <= 2;
}
void RadioInput(uint32_t io) {
  if (StuntWheel() && WheelSteering()) Wr8(io + 6, Rd8(io + 4));  // not down
}

// A call of CText::Get (thiscall, the key pushed; ret 4).
void StuntHelpText(Ctx& c) {
  const uint32_t base = c.esp, text = c.ecx;
  if (StuntWheel() && !UsingPad(c)) {
    static const char kKey[] = "USJ_FRST_KMB";
    c.esp -= 16;  // the key, below the frame
    std::memcpy(GPtr(c.esp), kKey, sizeof kKey);
    const uint32_t key = c.esp;
    c.ecx = text;
    if (uint8_t(CallGuest(c, 0x008A3CF0, {key}))) {  // CText::DoesTextLabelExist
      c.ecx = text;
      const uint32_t s = uint32_t(CallGuest(c, 0x008A4000, {key}));
      c.esp = base + 4;
      c.eax = s;
      return;
    }
    c.esp = base;
    c.ecx = text;
  }
  CallGuest(c, 0x008A4000, {});
  c.esp = base + 4;
}
}  // namespace
void hook_00A8D00D(Ctx& c) {  // subss xmm0, [128.0]
  float v = Lane0(c.xmm[0]);
  if (StuntWheel()) {
    const uint32_t frame = Rd32(kFrame);
    if (!g_stunt_seen || frame - g_stunt_frame > 2) g_stunt_value = 128.0f;  // a new jump
    g_stunt_seen = true;
    g_stunt_frame = frame;
    bool pad;
    int notches = 0;
    {
      KeepRegs keep(c);  // no call here in the game's code
      pad = UsingPad(c);
      if (!pad) {
        const uint32_t kb = uint32_t(CallGuest(c, 0x0086A1A0, {1}));
        if (kb && Down(kb + 0x2C78)) notches++;
        if (kb && Down(kb + 0x2C68)) notches--;
      }
    }
    if (!pad) {
      g_stunt_value = std::clamp(g_stunt_value + 32.0f * float(notches), 0.0f, 255.0f);
      v = g_stunt_value;
      g_wheel_seen = true;
      g_wheel_frame = frame;
    }
  }
  SetLane0(c.xmm[0], v - RdF32(0x00D74D30));
}
void hook_0080EDAE(Ctx& c) {  // lea ecx, [edi+29D8]: next station
  c.ecx = c.edi + 0x29D8;
  RadioInput(c.ecx);
}
void hook_0080EDD6(Ctx& c) {  // lea ecx, [edi+29E8]: previous station
  c.ecx = c.edi + 0x29E8;
  RadioInput(c.ecx);
}
void hook_00CC3ED4(Ctx& c) { StuntHelpText(c); }
void hook_00CC3F10(Ctx& c) { StuntHelpText(c); }

}  // namespace recomp::gen
