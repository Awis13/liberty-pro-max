// The community's fixes, done again for 1.0.8.0 by their ideas: from
// FusionFix (ThirteenAG and contributors, GPL-3.0; its code not taken)
// and ZolikaPatch (by its ini's descriptions only). Each one is an option
// (the launcher's Advanced tab sets them), on unless said otherwise:
//   GTAIV_LADDER_FIX: no stalls getting onto ladders at high frame rates;
//   GTAIV_DRAW_DISTANCE_FIX (off by default: 7x the draws at high sliders): the draw distance sliders count from the
//     start, not only once the graphics menu was opened;
//   GTAIV_RADIO_FIX: the radio station keeps playing after a cutscene;
//   GTAIV_INSTANT_MENUS: no waits entering and leaving the pause menu or
//     before it can be opened at start;
//   GTAIV_VEHICLE_BUDGET: twice the memory for vehicle models (streets of
//     nothing but taxis), and twice the vehicle structures to match;
//   GTAIV_BIGGER_POOLS: twice the task and pointer node pools;
//   GTAIV_DRAWABLE_LIMIT: room for 20000 drawable references, not 13000
//     (the crash in "In the Crosshairs" at the longest draw distance);
//   GTAIV_SAVE_IN_VEHICLE: the save menu a script asks for opens in a
//     vehicle too (FusionFix's quick save from the phone);
//   GTAIV_MOUSE_RECOIL: weapon recoil with the mouse as with a gamepad;
//   GTAIV_COVER_CAMERA: no forced camera centring at the edge of cover;
//   GTAIV_LOD_LIGHTS: the city's distant lights from 7 pm (the consoles'
//     time, not 10 pm), in cutscenes and seen from the street, and not
//     flickering in The Ballad of Gay Tony;
//   GTAIV_CURSOR_SIZE: the mouse cursor's size the same whatever the size
//     of its texture;
//   GTAIV_RETICLE_HEALTH: the aimed-at ped's health on the reticle with
//     keyboard and mouse too;
//   GTAIV_RADAR_ZOOM_DELAY: the radar stays zoomed out 3 s after the key;
//   GTAIV_TRAIN_LANES: cars do not swerve under trains on the bridges;
//   GTAIV_DATE_FIX: the date does not go back a day when dying, busted or
//     respraying in the evening;
//   GTAIV_NO_MEM_RESTRICT: streaming not held back by the memory budget
//     (the -nomemrestrict argument);
//   GTAIV_TIMER_FLOOR: the game's time step may go down to 1 ms, not
//     1/300 s (the game ran fast above 300 fps);
//   GTAIV_HELI_CEILING (off by default): helicopters fly up to 770 m, as
//     in The Ballad of Gay Tony, not 350 m;
//   GTAIV_RUN_IN_BACKGROUND (off by default): the game keeps running when
//     its window is not in front;
//   GTAIV_MENU_RIGHT_CLICK: the right mouse button goes back in menus;
//   GTAIV_ACTION_CAMERA=0: no cinematic camera when vaulting a wall or
//     jacking a car;
//   GTAIV_STUNT_JUMP_CAMERA=0: no cinematic camera and slow motion on
//     unique stunt jumps;
//   GTAIV_CONSOLE_REFLECTIONS: environment reflections at full strength
//     and cars spawning with dirt and reflections as on the consoles;
//   GTAIV_MIRROR_OFFSET: mirrors show decals close to them (graffiti in
//     the Middle Park toilet), the consoles' plane offset;
//   GTAIV_WATER_TILING: the water surface at 256x256 at any setting, the
//     consoles' tiling;
//   GTAIV_COP_HATS: cops take their hats off in vehicles, as on the
//     consoles;
//   GTAIV_COUNTER_DEATH_ANIM: no second, older death animation after a
//     melee counter (The Ballad of Gay Tony's fix);
//   GTAIV_REVERSING_LIGHTS: reversing lights at the tail lights, not on
//     the bonnet;
//   GTAIV_BIKE_FEET: on bikes the player puts a foot down while on the
//     phone and uses his feet going backwards (the episodes' fixes);
//   GTAIV_SNIPER_MOVEMENT: the player can walk while zoomed in with a
//     sniper rifle (the pad's zoom then on the d-pad);
//   GTAIV_RAIN_LIGHTING: rain lit by visualsettings.dat's values, as on
//     the consoles (the PC overwrote them with fixed ones);
//   GTAIV_HELI_SEARCHLIGHTS: with two or more helicopters only one shines
//     its searchlight at a time (for up to 10 s), no flickering fight;
//   GTAIV_NIGHT_SHADOW_RESOLUTION: the night shadows setting alone sets
//     their resolution (256, 512, 1024);
//   GTAIV_CENTERED_CAR_CAMERA, GTAIV_CENTERED_FOOT_CAMERA (off by
//     default): the camera behind the car or the player, not off to the
//     side;
//   GTAIV_BLOOM=0, GTAIV_DEPTH_OF_FIELD=0: no bloom, no depth of field
//     blur (the time cycle's values for them zeroed as they are read);
//   GTAIV_HIDE_CURSOR_ON_PAD: the menus' mouse cursor not drawn while a
//     gamepad is used;
//   GTAIV_BRAKE_LIGHTS_STOPPED (off by default): the player's brake
//     lights stay lit while the car stands, as the AI's do;
//   GTAIV_CAR_CAMERA_RETURN: with keyboard and mouse the car camera swings
//     back behind the car faster (a camera turn speed of 3), but
//     not while the mouse moves it;
//   GTAIV_MOUSE_CAMERA_DELAY: on foot the camera does not swing back behind
//     Niko for 2 s after the mouse moved it.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00A8DCF0_lifted(Ctx& c);
void f_00A8C920_lifted(Ctx& c);
void f_009C8200_lifted(Ctx& c);
void f_008C31B0_lifted(Ctx& c);

using namespace hook;


// CTaskComplexClimbLadder::CreateSubTask: the heading Niko turns to before
// climbing is reached within 0.02 rad (fld [k]), the slide to the ladder
// within 0.1. At high frame rates the turn finishes after the slide, out
// of the order the task expects, and he stands at the ladder for a while.
// The same 0.1 for both.
void hook_00C77C7D(Ctx& c) {
  static const bool on = OptionOn("GTAIV_LADDER_FIX", true);
  x87::LoadF32(c.fpu, on ? Bits(0.1f) : Rd32(0x00DD5CFC));
}

// CGame::Init calls an empty function here; the draw distance settings are
// worked out (00477300) only when the graphics menu is opened, so until
// then the sliders' values do not apply. Worked out here instead.
void hook_00493687(Ctx& c) {
  static const bool on = OptionOn("GTAIV_DRAW_DISTANCE_FIX", false);
  if (on) CallGuest(c, 0x00477300, {1});  // its char argument, as the menu passes (004764EB)
}

// After a cutscene (cmp byte [esp+4], 0; je): with no station asked for,
// the radio is retuned from its start. Kept playing as it was.
void hook_007F1448(Ctx& c) {
  static const bool on = OptionOn("GTAIV_RADIO_FIX", true);
  CmpFlags<1>(c, on ? 0 : Rd8(c.esp + 4), 0);
}

// The pause menu's delays: 400 ms before it opens, 800 ms before the game
// resumes, and none of it for 3 s after the start (cmp ecx, 3000; jb).
// FusionFix's least: 20, 0 and 300 ms.
namespace {
bool InstantMenus() {
  static const bool on = OptionOn("GTAIV_INSTANT_MENUS", true);
  return on;
}
}  // namespace
void hook_0047C735(Ctx& c) { Push(c, InstantMenus() ? 20 : 400); }
void hook_0047D3F0(Ctx& c) { Push(c, InstantMenus() ? 0 : 800); }
void hook_008578A5(Ctx& c) { CmpFlags<4>(c, c.ecx, InstantMenus() ? 300 : 3000); }

// The vehicle population's model memory budget ([00F49B98], 40 MB): when
// a few models fill it (taxis above all) no other model is streamed in,
// and the streets fill with taxis. Twice that; read in two places.
namespace {
constexpr uint32_t kVehicleBudget = 0x00F49B98;
uint32_t VehicleBudget() {
  static const bool on = OptionOn("GTAIV_VEHICLE_BUDGET", true);
  return on ? 2 * Rd32(kVehicleBudget) : Rd32(kVehicleBudget);
}
}  // namespace
void hook_00A41310(Ctx& c) { c.ecx -= VehicleBudget(); }  // sub ecx, [budget]
void hook_00A41FED(Ctx& c) { c.eax = VehicleBudget(); }   // mov eax, [budget]

// atPool's constructor (thiscall: count, name, element size): count *
// size bytes (ecx = edi * eax). Some pools are made twice the size: the
// tasks' (FusionFix), the pointer nodes' (ZolikaPatch), and the vehicle
// structures' with the larger vehicle budget (more models at once).
void hook_007F3DBC(Ctx& c) {
  static const bool pools = OptionOn("GTAIV_BIGGER_POOLS", true);
  static const bool vehicles = OptionOn("GTAIV_VEHICLE_BUDGET", true);
  const uint32_t name_va = Rd32(c.esp + 0x10);
  if (name_va) {
    const std::string_view name(reinterpret_cast<const char*>(GPtr(name_va)));
    if ((pools && (name == "Task" || name == "PtrNode Single" || name == "PtrNode Double")) ||
        (vehicles && name == "VehicleStruct"))
      c.edi *= 2;
  }
  c.ecx = c.edi * c.eax;
}

// The drawable reference list's size (push 13000): with the longest draw
// distance it overflows in "In the Crosshairs" and the game crashes.
void hook_00B72130(Ctx& c) {
  static const bool on = OptionOn("GTAIV_DRAWABLE_LIMIT", true);
  Push(c, on ? 20000 : 13000);
}

// ACTIVATE_SAVE_MENU's native: nothing while the player is in a vehicle
// (test byte [eax+26C], 4; jne). Let through.
void hook_00B4EDC7(Ctx& c) {
  static const bool on = OptionOn("GTAIV_SAVE_IN_VEHICLE", true);
  TestFlags8(c, on ? 0 : Rd8(c.eax + 0x26C) & 4);
}

// CWeapon::DoAccuracy: with the mouse the recoil is scaled by 0.1 (mulss
// xmm0, [k]); FusionFix's 0.65 is nearer the gamepad's.
void hook_009D1D9E(Ctx& c) {
  static const bool on = OptionOn("GTAIV_MOUSE_RECOIL", true);
  SetLane0(c.xmm[0], Lane0(c.xmm[0]) * (on ? 0.65f : RdF32(0x00D9D9D8)));
}

// CTaskComplexNewUseCover: the time at the edge of cover adds up (movss
// xmm0, [time step]) until the camera is centred by force. Never.
void hook_00C3E61D(Ctx& c) {
  static const bool on = OptionOn("GTAIV_COVER_CAMERA", true);
  LoadSs(c.xmm[0], on ? 3.4e38f / 2.0f : RdF32(0x010FCB74));
}

// The city's distant lights (and far vehicle impostors): from 22:00 on
// the PC, 19:00 on the consoles (an hour offset of 3, [00F60690], and of 1
// in the morning, [00F60688]); not in cutscenes (a call of the "is a
// cutscene running" check, 007F02B0); and only above 75 m, faded in up to
// 115 (subss xmm0, [75.0]; the consoles from the ground).
namespace {
bool LodLights() {
  static const bool on = OptionOn("GTAIV_LOD_LIGHTS", true);
  return on;
}
}  // namespace
void hook_00CA1B79(Ctx& c) { c.ecx = LodLights() ? 0 : Rd32(0x00F60690); }  // mov ecx, [k]
void hook_00CA1BDF(Ctx& c) { c.ecx -= LodLights() ? 0 : Rd32(0x00F60688); }  // sub ecx, [k]
void hook_00CA1B4E(Ctx& c) {  // call 7F02B0; test al, al (jne: none drawn)
  const uint8_t cutscene = LodLights() ? 0 : uint8_t(CallGuest(c, 0x007F02B0, {}));
  TestFlags8(c, cutscene);
}
void hook_00C9FA5D(Ctx& c) {
  if (!LodLights()) SetLane0(c.xmm[0], Lane0(c.xmm[0]) - RdF32(0x00D9D640));
}
// In The Ballad of Gay Tony (cmp [episode], 2; jne) the same function
// halves five of the lights' values after its loops, and they flicker at
// some camera angles. Skipped in every episode.
void hook_00C9FF04(Ctx& c) { CmpFlags<4>(c, LodLights() ? 0 : Rd32(0x0112D754), 2); }

// The mouse cursor's size: its texture's width over 1024 and height over
// 768 (mulss by [00EDF3B8], [00EDF878]), a share of the screen whatever
// its resolution, so a larger cursor texture is drawn larger. 30 / 1024
// of the width and 30 / 768 of the height instead, whatever the texture's
// size; with a texture of about 30 pixels it looks the same.
namespace {
bool CursorSize() {
  static const bool on = OptionOn("GTAIV_CURSOR_SIZE", true);
  return on;
}
}  // namespace
void hook_00486AE8(Ctx& c) {  // movss [esp+20], xmm0: width
  WrF32(c.esp + 0x20, CursorSize() ? 30.0f / 1024.0f : Lane0(c.xmm[0]));
}
void hook_00486B03(Ctx& c) {  // movss [esp+28], xmm0: height
  WrF32(c.esp + 0x28, CursorSize() ? 30.0f / 768.0f : Lane0(c.xmm[0]));
}

// The health of the ped aimed at is drawn on the reticle only with a
// gamepad (or in multiplayer): both checks passed.
namespace {
bool ReticleHealth() {
  static const bool on = OptionOn("GTAIV_RETICLE_HEALTH", true);
  return on;
}
}  // namespace
void hook_004A88FB(Ctx& c) {  // cmp byte [ecx+328D], 0; je
  CmpFlags<1>(c, ReticleHealth() ? 0 : Rd8(c.ecx + 0x328D), 0);
}
void hook_004A8866(Ctx& c) {  // test al, al; jne
  TestFlags8(c, ReticleHealth() ? 1 : uint8_t(c.eax));
}

// CRadarNY: the radar is zoomed out while the key is held (004B3770 says
// whether it is); it stays out 3 s after.
void hook_004B29F2(Ctx& c) {
  static const bool on = OptionOn("GTAIV_RADAR_ZOOM_DELAY", true);
  static uint32_t until;  // the game's clock, ms
  const uint32_t held = uint32_t(CallGuest(c, 0x004B3770, {})) & 0xFF;
  if (!on) {
    c.eax = held;
    return;
  }
  const uint32_t now = Rd32(0x010FCB6C);
  constexpr uint32_t kDelay = 3000;
  if (held) until = now + kDelay;
  else if (until - now > kDelay) until = 0;  // a clock reset (a save loaded)
  c.eax = held || now < until ? 1 : 0;
}

// CCarAI::WeaveThroughCarsSectorList: a vehicle within 8 m up or down is
// in the way (movss xmm1, [8.0]); a train on the bridge above or below
// then makes cars swerve lanes. 2.5 m for trains (type 3 at +1354).
void hook_00C606F9(Ctx& c) {
  static const bool on = OptionOn("GTAIV_TRAIN_LANES", true);
  LoadSs(c.xmm[1], on && Rd32(c.esi + 0x1354) == 3 ? 2.5f : RdF32(0x00D6E58C));
}

// After dying, being busted or a respray the clock is moved on by a call
// (hours, minutes, -1, day) given -1 for the day and the day where -1
// belongs: between noon and 23:00 the date goes back a day. Pushed the
// right way round.
void hook_007D44F9(Ctx& c) {  // push -1; push esi
  static const bool on = OptionOn("GTAIV_DATE_FIX", true);
  Push(c, on ? c.esi : 0xFFFFFFFF);
  Push(c, on ? 0xFFFFFFFF : c.esi);
}

// The streaming memory: limited unless -nomemrestrict was given (cmp
// [arg], 0; jne). As if it was.
void hook_0040F152(Ctx& c) {
  static const bool on = OptionOn("GTAIV_NO_MEM_RESTRICT", true);
  CmpFlags<4>(c, on ? 1 : Rd32(0x010AAB78), 0);
}

// CTimer::Initialise: the time step's floor, 1/300 s (movss [00F2E09C],
// xmm0); above 300 fps every frame counted 1/300 s and the game ran fast.
// 1 ms. (Its ceiling, 1/15 s, stays: slower below 15 fps is meant.)
void hook_007CBB52(Ctx& c) {
  static const bool on = OptionOn("GTAIV_TIMER_FLOOR", true);
  WrF32(0x00F2E09C, on ? 0.001f : Lane0(c.xmm[0]));
}

// Helicopters' height limit: 770 m in The Ballad of Gay Tony (episode
// [0112D754] >= 2), else 350 (cmp; jl). Optional, off by default.
void hook_008F0071(Ctx& c) {
  static const bool on = OptionOn("GTAIV_HELI_CEILING", false);
  CmpFlags<4>(c, on ? 2 : Rd32(0x0112D754), 2);
}

// Whether the game blocks while its window is not in front
// (grcDevice::ms_bNoBlockOnLostFocus, [00FE80D3], set from the settings at
// the start, just before and ax, 3FF). Kept running with the option.
void hook_004063D2(Ctx& c) {
  static const bool on = OptionOn("GTAIV_RUN_IN_BACKGROUND", false);
  c.eax = (c.eax & 0xFFFF0000u) | (c.eax & 0x3FF);
  if (on) Wr8(0x00FE80D3, 1);
}

// Menus take a click of the left button (test reg, 1) to choose and to go
// back; the right one (2) goes back too: where the menu checks for "back"
// either does, and where it chooses a row a right click is remembered and
// counts as "back" (the check after it, test al, al).
namespace {
bool MenuRightClick() {
  static const bool on = OptionOn("GTAIV_MENU_RIGHT_CLICK", true);
  return on;
}
bool g_right_click;
}  // namespace
void hook_00865ADE(Ctx& c) { TestFlags8(c, uint8_t(c.edx) & (MenuRightClick() ? 3 : 1)); }  // test dl, 1
void hook_008661B9(Ctx& c) { TestFlags8(c, uint8_t(c.ecx) & (MenuRightClick() ? 3 : 1)); }  // test cl, 1
void hook_00866235(Ctx& c) {  // test dl, 1
  g_right_click = MenuRightClick() && (c.edx & 2);
  TestFlags8(c, uint8_t(c.edx) & 1);
}
void hook_0086648D(Ctx& c) {  // add esp, 1C (then test al, al: "back")
  c.esp += 0x1C;
  // Only a right click of this frame (the buttons now at [017945CC], the
  // frame before at [017945C4]): one remembered on a frame that did not
  // get here does not count later.
  const uint32_t buttons = Rd32(0x017945CC);
  const bool clicked = (buttons ^ Rd32(0x017945C4)) & buttons & 2;
  if ((c.eax & 0xFF) == 0 && g_right_click && clicked) c.eax = (c.eax & ~0xFFu) | 1;
  g_right_click = false;
}

// The cinematic action camera's triggers (thiscall, returning whether to
// cut to it): CIntermezzoEventVaultWall and ...PlayerJackVehicle.
namespace {
bool ActionCamera() {
  static const bool on = OptionOn("GTAIV_ACTION_CAMERA", true);
  return on;
}
}  // namespace
void f_00A8DCF0(Ctx& c) {
  if (ActionCamera()) return f_00A8DCF0_lifted(c);
  c.eax = 0;
  c.esp += 4;  // ret
}
void f_00A8C920(Ctx& c) {
  if (ActionCamera()) return f_00A8C920_lifted(c);
  c.eax = 0;
  c.esp += 4;  // ret
}

// CStuntJumpManager::Update on a unique stunt jump: the cinematic camera
// (push ecx; mov ecx, 011CAEC0; call 850890) and then the slow motion
// unless (call 6C1A10; test al, al; jne) says not. Both skipped when off.
namespace {
bool StuntJumpCamera() {
  static const bool on = OptionOn("GTAIV_STUNT_JUMP_CAMERA", true);
  return on;
}
}  // namespace
void hook_00CC3F3A(Ctx& c) {
  if (!StuntJumpCamera()) return;
  const uint32_t arg = c.ecx;
  c.ecx = 0x011CAEC0;
  CallGuest(c, 0x00850890, {arg});
}
void hook_00CC3F45(Ctx& c) {
  TestFlags8(c, StuntJumpCamera() ? uint8_t(CallGuest(c, 0x006C1A10, {})) : 1);
}

// Reflections: the environment's are scaled by 0.6 on the PC (movss xmm1,
// [00F498F0]); 1.0 as on the consoles. And cars spawn clean with weaker
// reflections unless a check passes (cmp [ebp+84], 0; jne and test eax,
// eax; jne): any car can spawn dirty, as on the consoles.
namespace {
bool ConsoleReflections() {
  static const bool on = OptionOn("GTAIV_CONSOLE_REFLECTIONS", true);
  return on;
}
}  // namespace
void hook_00A38B65(Ctx& c) { LoadSs(c.xmm[1], ConsoleReflections() ? 1.0f : RdF32(0x00F498F0)); }
void hook_008FEAEC(Ctx& c) { CmpFlags<4>(c, ConsoleReflections() ? 1 : Rd32(c.ebp + 0x84), 0); }
void hook_00B98343(Ctx& c) { CmpFlags<4>(c, ConsoleReflections() ? 0 : c.eax, 0); }

// Mirrors: their plane is moved 0.035 out (movss xmm1, [00F41AA8]), past
// decals on the wall; the consoles' 0.0125.
void hook_00901929(Ctx& c) {
  static const bool on = OptionOn("GTAIV_MIRROR_OFFSET", true);
  LoadSs(c.xmm[1], on ? 0.0125f : RdF32(0x00F41AA8));
}

// The water surface's render target grows with the water quality setting
// ([010FC324], a shift), and the water's tiling with it; the consoles'
// 256x256 (quality 1) always.
namespace {
uint32_t WaterQuality() {
  static const bool on = OptionOn("GTAIV_WATER_TILING", true);
  return on ? 1 : Rd32(0x010FC324);
}
}  // namespace
void hook_009AA8AA(Ctx& c) { c.ecx = WaterQuality(); }  // mov ecx, [quality]
void hook_009ADB46(Ctx& c) { c.ecx = WaterQuality(); }
void hook_009AFC40(Ctx& c) { c.ecx = WaterQuality(); }

// CCamFollowPed: how strongly the camera swings back behind the player
// this frame (movss [esp+48], xmm0). With the mouse it pulls against the
// player's aim at once; held at 0 for 2 s of game time after the mouse
// moved (its deltas this frame at [017945D0], [017945C0]). A gamepad
// leaves the mouse still, and the camera as the game has it.
void hook_00A8A6E0(Ctx& c) {
  static const bool on = OptionOn("GTAIV_MOUSE_CAMERA_DELAY", true);
  static uint32_t until, last;
  float v = Lane0(c.xmm[0]);
  if (on) {
    const uint32_t now = Rd32(0x010FCB6C);  // the game's clock, ms
    if (now < last) until = 0;               // a save loaded
    last = now;
    if (Rd32(0x017945D0) || Rd32(0x017945C0)) until = now + 2000;
    if (now < until) v = 0.0f;
  }
  WrF32(c.esp + 0x48, v);
}

// A ped's hat in a vehicle stays on for some models: the first two
// checked (cmp eax, [model]; je: keep it) are the regular and the fat
// cop's. Not them: their hats come off, as on the consoles.
namespace {
bool CopHats() {
  static const bool on = OptionOn("GTAIV_COP_HATS", true);
  return on;
}
}  // namespace
void hook_009B95F2(Ctx& c) { CmpFlags<4>(c, CopHats() ? ~Rd32(0x01368618) : c.eax, Rd32(0x01368618)); }
void hook_009B95FA(Ctx& c) { CmpFlags<4>(c, CopHats() ? ~Rd32(0x01368D20) : c.eax, Rd32(0x01368D20)); }

// After a melee counter the ped plays an older death animation as well
// (the jne at 00C1A368, its flags from before): never, as The Ballad of
// Gay Tony has it. Here: mov ebx, 1 just before it.
void hook_00C1A363(Ctx& c) {
  static const bool on = OptionOn("GTAIV_COUNTER_DEATH_ANIM", true);
  c.ebx = 1;
  if (on) {
    Flags f;
    UnpackEFlags(f, c.eflags);
    f.zf = true;
    c.eflags = PackEFlags(f);
  }
}

// The reversing lights are placed at a bone the model's vtable gives:
// slot 64h, the headlights' (mov eax, [edx+64]); slot 60h, the tail
// lights'.
void hook_008F8846(Ctx& c) {
  static const bool on = OptionOn("GTAIV_REVERSING_LIGHTS", true);
  c.eax = Rd32(c.edx + (on ? 0x60 : 0x64));
}

// Bikes: a foot down when standing on the phone only in the episodes
// (cmp [episode], 1; jl), and feet for reversing only in TLAD (cmp
// [episode], eax; jne). In IV too.
namespace {
bool BikeFeet() {
  static const bool on = OptionOn("GTAIV_BIKE_FEET", true);
  return on;
}
}  // namespace
void hook_00B67F1B(Ctx& c) { CmpFlags<4>(c, BikeFeet() ? 1 : Rd32(0x0112D754), 1); }
void hook_00BCB925(Ctx& c) { CmpFlags<4>(c, BikeFeet() ? c.eax : Rd32(0x0112D754), c.eax); }

// Zoomed in with a sniper rifle (weapon type 6: cmp [eax+4], 6; jne) the
// player cannot move. Let through; the left stick then moves him, so the
// pad's zoom axis (call 8A5150 with the stick's input, a float in st0)
// comes from the d-pad instead (the pad at ebx, its d-pad down / up
// bytes at +27EE / +27FE), with the stick's dead zone of a quarter.
namespace {
bool SniperMovement() {
  static const bool on = OptionOn("GTAIV_SNIPER_MOVEMENT", true);
  return on;
}
}  // namespace
void hook_00A612D4(Ctx& c) { CmpFlags<4>(c, SniperMovement() ? 0 : Rd32(c.eax + 4), 6); }
void hook_00A81E06(Ctx& c) {
  if (!SniperMovement()) {
    CallGuest(c, 0x008A5150, {Rd32(c.esp)});
    return;
  }
  const bool down = Rd8(c.ebx + 0x27EE) != 0, up = Rd8(c.ebx + 0x27FE) != 0;
  const float v = up ? 1.0f : down ? -1.0f : 0.0f;
  const float zone = 0.25f;
  const float out = v > zone ? (v - zone) / (1 - zone) : v < -zone ? (v + zone) / (1 - zone) : 0.0f;
  x87::LoadF32(c.fpu, Bits(out));
}

// The rain's lighting (diffuse, ambient, wrap scale and bias): read from
// visualsettings.dat, then overwritten on the PC by fixed values loaded
// and stored into the shader's parameters ([esp+20..38]). Left as read.
namespace {
bool RainLighting() {
  static const bool on = OptionOn("GTAIV_RAIN_LIGHTING", true);
  return on;
}
// movss [esp+off], xmm0 (then movss xmm0, [next] when next is given).
void RainStore(Ctx& c, uint32_t off, uint32_t next) {
  if (RainLighting()) return;
  std::memcpy(GPtr(c.esp + off), c.xmm[0].data(), 4);
  if (next) LoadSs(c.xmm[0], RdF32(next));
}
}  // namespace
void hook_00A63C90(Ctx& c) {
  if (!RainLighting()) LoadSs(c.xmm[0], RdF32(0x00F4B0BC));
}
void hook_00A63CAD(Ctx& c) { RainStore(c, 0x20, 0x00F4B0C0); }
void hook_00A63CC0(Ctx& c) { RainStore(c, 0x28, 0x00F4B0C4); }
void hook_00A63CCF(Ctx& c) { RainStore(c, 0x30, 0x01408308); }
void hook_00A63CE1(Ctx& c) { RainStore(c, 0x38, 0); }

// CHeli::PreRender (thiscall): a helicopter's searchlight is drawn by the
// first helicopter in a frame that gets to it (the frame it was last
// drawn, [01367AD4], against the frame counter); with two they take turns
// and the light flickers wildly. The one that has it keeps it, up to 10 s
// at a time, while its light is on (the game's own checks before drawing
// it: on +1FBC, brightness +1FB4 above 0, not off +2080); the others' turn
// checks fail.
void f_009C8200(Ctx& c) {
  static const bool on = OptionOn("GTAIV_HELI_SEARCHLIGHTS", true);
  static uint32_t owner, until;
  if (on) {
    const uint32_t heli = c.ecx, frame = Rd32(0x010FCC30);
    const bool lit = Rd8(heli + 0x1FBC) != 0 && RdF32(heli + 0x1FB4) > 0.0f && Rd8(heli + 0x2080) == 0;
    if (lit) {
      const uint32_t now = Rd32(0x010FCB6C);
      if (!owner || owner == heli || now > until) {
        owner = heli;
        until = now + 10000;
        Wr32(0x01367AD4, frame - 1);  // its turn
      } else {
        Wr32(0x01367AD4, frame);  // drawn already this frame
      }
    } else if (owner == heli) {
      owner = 0;
    }
  }
  f_009C8200_lifted(c);
}

// The night shadows' resolution (no arguments, the size in eax): 64 <<
// the shadow quality setting ([010FC31C]) up to 1024, when the night
// shadows setting ([010FC318]) is on at all; the latter's levels did not
// change their sharpness. By it alone instead: off, 256, 512, 1024 (night
// shadows also with the shadow quality off).
void f_008C31B0(Ctx& c) {
  static const bool on = OptionOn("GTAIV_NIGHT_SHADOW_RESOLUTION", true);
  if (!on) return f_008C31B0_lifted(c);
  const int32_t level = int32_t(Rd32(0x010FC318));
  c.eax = level <= 0 ? 0 : level == 1 ? 256 : level == 2 ? 512 : 1024;
  c.esp += 4;  // ret
}

// Centred cameras, options: CCamFollowVehicle moves the camera to the
// side when ms_bAllowSideOffset ([00F4B56F]; cmp byte, 0; je); CCamFollowPed
// looks past the player's shoulder by ms_targetOffset (0.2; movss xmm4,
// [00F4C2E0]).
void hook_00A7BF8D(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CENTERED_CAR_CAMERA", false);
  CmpFlags<1>(c, on ? 0 : Rd8(0x00F4B56F), 0);
}
void hook_00A89930(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CENTERED_FOOT_CAMERA", false);
  LoadSs(c.xmm[4], on ? 0.0f : RdF32(0x00F4C2E0));
}

// timecyc.dat: each line is read by one sscanf (call CF874F; cdecl: line,
// format, then a pointer per column). Its bloom intensity (column 38) and
// near and far depth of field blur (123, 124) zeroed after it when the
// options turn them off.
void hook_00A3B995(Ctx& c) {
  static const bool bloom = OptionOn("GTAIV_BLOOM", true);
  static const bool dof = OptionOn("GTAIV_DEPTH_OF_FIELD", true);
  c.eax = uint32_t(CallGuest(c, 0x00CF874F, {}));  // the arguments are already pushed
  auto column = [&](uint32_t k) { return Rd32(c.esp + 8 + 4 * k); };
  if (!bloom) WrF32(column(38), 0.0f);
  if (!dof) {
    WrF32(column(123), 0.0f);
    WrF32(column(124), 0.0f);
  }
}

// The menus' mouse cursor (004869D0) is drawn unless (with a gamepad:
// the pad's byte +328D is 0) the frontend is in state 3 and 7BDC10 says
// so; with a gamepad the cursor otherwise hangs in the menu. Not drawn
// with a gamepad at all: the pad seen at the first check, the state
// check passed, the last one (test al, al; jne: skip the drawing) taken.
namespace {
bool g_pad_menu;
bool HideCursorOnPad() {
  static const bool on = OptionOn("GTAIV_HIDE_CURSOR_ON_PAD", true);
  return on;
}
}  // namespace
void hook_004869E1(Ctx& c) {  // cmp byte [eax+328D], 0; jne: draw
  const uint8_t b = Rd8(c.eax + 0x328D);
  g_pad_menu = HideCursorOnPad() && b == 0;
  CmpFlags<1>(c, b, 0);
}
void hook_004869EA(Ctx& c) {  // cmp [010FBF24], 3; jne: draw
  CmpFlags<4>(c, g_pad_menu ? 3 : Rd32(0x010FBF24), 3);
}
void hook_004869FD(Ctx& c) {  // test al, al; jne: skip the drawing
  TestFlags8(c, g_pad_menu ? 1 : uint8_t(c.eax));
}

// CVehicle::DoVehicleLights: the brake lights are lit by the brake pedal
// (movss xmm0, [ebx+10CC]; comiss xmm0, [0.1]). The AI holds the brake
// standing still; the player lets go, so his go dark at the lights. Lit
// while the player's car (driver at +FA0) stands with no throttle (+10C8):
// slower than 0.5 along its forward axis (CPhysical::GetLocalSpeed,
// 8E50F0: thiscall out, offset, 0, 0; the matrix at +20, forward at +10).
void hook_008FC1A5(Ctx& c) {
  static const bool on = OptionOn("GTAIV_BRAKE_LIGHTS_STOPPED", false);
  float brake = Lane0(c.xmm[0]);
  if (on && brake <= 0.1f && RdF32(c.ebx + 0x10C8) <= 0.01f) {
    const uint32_t veh = c.ebx, driver = Rd32(veh + 0xFA0);
    KeepRegs keep(c);  // no calls here in the game's code
    if (driver && driver == uint32_t(CallGuest(c, 0x007CD230, {}))) {  // FindPlayerPed
      c.esp -= 32;
      const uint32_t out = c.esp, off = c.esp + 16;
      std::memset(GPtr(c.esp), 0, 32);
      c.ecx = veh;
      CallGuest(c, 0x008E50F0, {out, off, 0, 0});
      c.esp += 32;
      const uint32_t m = Rd32(veh + 0x20);
      const float fwd = RdF32(out) * RdF32(m + 0x10) + RdF32(out + 4) * RdF32(m + 0x14) + RdF32(out + 8) * RdF32(m + 0x18);
      if (std::fabs(fwd) < 0.5f) brake = 1.0f;
    }
  }
  SetLane0(c.xmm[0], brake);
  ComissFlags(c, brake, RdF32(0x00DDDCCC));
}

// CCamFollowVehicle: how fast the camera swings back behind the car this
// frame, across and up (divss by a time; movss to the frame). With
// keyboard and mouse divided by 0.7 more (a camera turn speed of 3: 1 -
// 3 * 0.1), and none on a frame the mouse moves (its deltas at
// [017945D0], [017945C0]), so it does not pull against the player's
// look; with a gamepad as it was (the pad, 86A1A0(1), has its "keyboard"
// byte +328D at 0 then).
namespace {
float CarCameraReturn(Ctx& c, float v, float time) {
  static const bool on = OptionOn("GTAIV_CAR_CAMERA_RETURN", true);
  if (!on) return v / time;
  KeepRegs keep(c);  // no call here in the game's code
  const uint32_t pad = uint32_t(CallGuest(c, 0x0086A1A0, {1}));
  if (pad && Rd8(pad + 0x328D) == 0) return v / time;
  if (Rd32(0x017945D0) || Rd32(0x017945C0)) return 0.0f;
  return v / (time * 0.7f);
}
}  // namespace
void hook_00A813F9(Ctx& c) {  // divss xmm0, xmm1; movss [esp+58], xmm0
  const float v = CarCameraReturn(c, Lane0(c.xmm[0]), Lane0(c.xmm[1]));
  SetLane0(c.xmm[0], v);
  WrF32(c.esp + 0x58, v);
}
void hook_00A81108(Ctx& c) {  // divss xmm0, xmm2; movss [esp+BC], xmm0
  const float v = CarCameraReturn(c, Lane0(c.xmm[0]), Lane0(c.xmm[2]));
  SetLane0(c.xmm[0], v);
  WrF32(c.esp + 0xBC, v);
}

}  // namespace recomp::gen
