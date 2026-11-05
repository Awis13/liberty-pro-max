// The community's high frame rate fixes, done again for 1.0.8.0 by their
// ideas (FusionFix's frameratevigilante, GPL-3.0; its code not taken).
// Each an option the launcher sets, on unless said otherwise:
//   GTAIV_PHYSICS_STEP: the physics' pre- and post-simulation steps no
//     shorter than 1/150 s (vehicles, ragdolls and objects overshoot less
//     at high frame rates);
//   GTAIV_SLIDE_OBJECT: objects scripts slide (doors, gates, lifts) move
//     as fast at any frame rate;
//   GTAIV_SHAKE_FIX: camera shake as strong, and with its jolts, at any
//     frame rate;
//   GTAIV_CAMERA_SHAKE=0: no hand-held camera shake at all;
//   GTAIV_WATER_FLIP: the water's buffers flipped only once the render
//     thread is done with them (flicker);
//   GTAIV_MINIGAME_30FPS: 30 fps in the minigames made for it (pool, air
//     hockey, arm wrestling, bowling, drinking);
//   GTAIV_RADAR_EVERY_FRAME: the radar drawn anew every frame, not every
//     30 ms (it moved in steps at high frame rates), its zoom per time;
//   GTAIV_HOOD_CAMERA: the bonnet and bumper cameras' spring sways the
//     same at any frame rate (it jittered at 120 fps);
//   GTAIV_HELI_ROTOR_DAMAGE: helicopter rotors and tails break off after
//     as much bumping at any frame rate;
//   GTAIV_LOADING_ANIMATION: loading screen pictures move at the speed
//     the consoles show them (the PC's ran twice as fast);
//   GTAIV_CUTSCENE_CLOCK: cutscenes' animations run smoothly, locked to
//     their audio, not in the audio position's steps;
//   GTAIV_CUTSCENE_CAMERA: cutscene cameras without fits of zooming and
//     without flicker at their cuts at high frame rates (ZolikaPatch's
//     cutscene fixes; FusionFix's cutscene camera).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_008CEF80_lifted(Ctx& c);
void f_00AD1710_lifted(Ctx& c);
void f_007F16B0_lifted(Ctx& c);
void f_00A7CFC0_lifted(Ctx& c);

using namespace hook;

namespace {

float StepsOf30() { return RdF32(0x010FCB74) * 30.0f; }  // CTimer::ms_fTimeStep

}  // namespace

// CPhysics::Update: each time slice runs the pre-simulation, the
// simulation and the post-simulation with the slice's step (movss [esp],
// xmm; the argument). At high frame rates the pre- and post-simulation
// steps are so short that vehicles, ragdolls and objects overshoot; they
// get at least 1/150 s (twice the 1/300 floor of a frame), the simulation
// itself its own step.
namespace {
void PhysicsStepArg(Ctx& c, int reg) {
  static const bool on = OptionOn("GTAIV_PHYSICS_STEP", true);
  float v;
  std::memcpy(&v, c.xmm[reg].data(), 4);
  WrF32(c.esp, on ? std::max(v, 1.0f / 150.0f) : v);
}
}  // namespace
void hook_009252C4(Ctx& c) { PhysicsStepArg(c, 1); }  // PreSimUpdate's
void hook_009252FC(Ctx& c) { PhysicsStepArg(c, 0); }  // PostSimUpdate's

// SLIDE_OBJECT's native (cdecl: object, x, y, z, x speed, y speed, z
// speed, collide): the speeds are per frame, and scripts call it each
// frame: at 120 fps objects slid four times as fast. Scaled by the step
// against 1/30 s.
void f_00AD1710(Ctx& c) {
  static const bool on = OptionOn("GTAIV_SLIDE_OBJECT", true);
  if (on)
    for (uint32_t off : {0x14u, 0x18u, 0x1Cu}) WrF32(c.esp + off, RdF32(c.esp + off) * StepsOf30());
  f_00AD1710_lifted(c);
}

// CHandShaker::Process (thiscall, float scale; ret 4): the hand-held camera
// shake (on foot, in cars, drunk, the helicopters' searchlights). Each
// frame it pushes each axis' speed (+70) by a random amount, and with a
// chance of one in +98 frames at 30 fps it kicks all three (by up to
// +9C); then turns the angles (+40) by the speeds times the camera step *
// 30, keeps them within +50 and makes the matrix at `this` from the angles
// times the scale (rage::Matrix34::FromEulersXYZ, 446140). The pushes come
// once a frame, so at 120 fps four times as many; and the kick's chance is
// worked out with the step cut to a whole number, 0 above 30 fps: shakes
// grew stronger and lost their jolts. Pushes and kicks at 30 Hz of game
// time here, the turning per frame as before (FusionFix's hand shaker, by
// its idea). With GTAIV_CAMERA_SHAKE=0 the camera does not shake (the
// searchlights, scale 75, still do).
namespace {

struct ShakeTicks {
  std::mutex mu;
  std::unordered_map<uint32_t, float> due;  // by CHandShaker
  std::minstd_rand rng{12345};
};

}  // namespace

void f_008CEF80(Ctx& c) {
  static const bool fix = OptionOn("GTAIV_SHAKE_FIX", true);
  static const bool shake = OptionOn("GTAIV_CAMERA_SHAKE", true);
  if (!fix) {
    // The game's own shaker; without the shake its matrix is made from
    // angles times 0 (the scale is used for nothing else).
    if (!shake && RdF32(c.esp + 4) != 75.0f) WrF32(c.esp + 4, 0.0f);
    f_008CEF80_lifted(c);
    return;
  }
  static ShakeTicks ticks;
  const uint32_t self = c.ecx;
  const float scale = RdF32(c.esp + 4);
  auto F = [&](uint32_t off) { return RdF32(self + off); };
  auto Set = [&](uint32_t off, float v) { WrF32(self + off, v); };

  // The camera's step * 30; in replays the replay's.
  float steps = RdF32(0x010FCBE4) * 30.0f;
  if (Rd32(0x0112EAC0) == 1 || Rd32(0x011402D4) != Rd32(0x00E71574) || Rd32(0x00F30468) == 0x12)
    steps = float(Rd32(0x0112E738)) * 0.001f * 30.0f;

  int runs = 0;
  {
    std::lock_guard<std::mutex> l(ticks.mu);
    float& t = ticks.due[self];
    t += RdF32(0x010FCB74);  // CTimer::ms_fTimeStep
    for (; t >= 1.0f / 30.0f && runs < 4; runs++) t -= 1.0f / 30.0f;
    if (t > 1.0f / 30.0f) t = 0;  // after a long frame: no catching up beyond that
  }
  for (int r = 0; r < runs; r++) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float lo = F(0x90), hi = F(0x94);
    for (uint32_t i = 0; i < 12; i += 4) {
      const float angle = F(0x40 + i), speed = F(0x70 + i);
      float s = std::fabs(angle / F(0x50 + i)) * (hi - lo) + lo;
      if ((angle > 0 && speed > 0) || (angle < 0 && speed < 0)) s *= F(0x80 + i);  // outward: damped
      float push;
      {
        std::lock_guard<std::mutex> l(ticks.mu);
        push = unit(ticks.rng) * F(0x60 + i) * s;
      }
      Set(0x70 + i, speed + (angle > 0 ? -push : push));
    }
    const int every = int(F(0x98));
    std::lock_guard<std::mutex> l(ticks.mu);
    if (int(unit(ticks.rng) * float(every - 1)) == 1) {
      const float k = F(0x9C);
      for (uint32_t i = 0; i < 12; i += 4) Set(0x70 + i, F(0x70 + i) + unit(ticks.rng) * 2 * k - k);
    }
  }

  float e[3];
  for (uint32_t i = 0; i < 3; i++) {
    const float limit = F(0x50 + 4 * i);
    const float a = std::clamp(F(0x40 + 4 * i) + F(0x70 + 4 * i) * steps, -limit, limit);
    Set(0x40 + 4 * i, a);
    e[i] = shake || scale == 75.0f ? a * scale : 0.0f;
  }
  c.esp -= 16;  // the angles, below the frame
  const uint32_t v = c.esp;
  for (uint32_t i = 0; i < 3; i++) WrF32(v + 4 * i, e[i]);
  WrF32(v + 12, 0.0f);
  c.ecx = self;
  CallGuest(c, 0x00446140, {v});  // rage::Matrix34::FromEulersXYZ (thiscall)
  c.esp += 16;
  c.esp += 8;  // ret 4
}

// Cutscene cameras (CCamAnimated). The PC version added two checks to the
// cutscene camera's update (an episode and a mode, before 00C9A866) that
// at high frame rates make it zoom in fits; skipped. The camera holds its
// last phase near a cut (flagged in the animation in 30 fps frames) and
// stores the 30 fps frame it held at (+1B4) to keep holding it: not
// stored, the hold stays with the frames really at the cut. And the
// phase update (UpdateCameraFromPhase, C9A640) is run both with and
// without that hold: without it unless the two differ as a cut does.
namespace {

bool CutsceneCamera() {
  static const bool on = OptionOn("GTAIV_CUTSCENE_CAMERA", true);
  return on;
}

uint8_t g_hold_at_cut = 1;  // the update's "hold near a cut" flag
double g_since_cut;         // game time since the last cut taken, s

constexpr uint32_t kUpdateFromPhase = 0x00C9A640;
constexpr uint32_t kCamSize = 0x4F8;
constexpr uint32_t kCamFront = 0x20, kCamPos = 0x40;  // two Vector3s of its matrix

bool Moved(const uint8_t* a, const uint8_t* b, uint32_t off, float most) {
  float x[3], y[3];
  std::memcpy(x, a + off, 12);
  std::memcpy(y, b + off, 12);
  for (int i = 0; i < 3; i++)
    if (std::fabs(x[i] - y[i]) > most) return true;
  return false;
}

// call C9A640 (thiscall, float phase; ret 4) with the phase pushed.
void UpdateFromPhase(Ctx& c) {
  const uint32_t self = c.ecx, phase = Rd32(c.esp);
  auto run = [&](uint8_t hold) {
    g_hold_at_cut = hold;
    c.ecx = self;
    return uint32_t(CallGuest(c, kUpdateFromPhase, {phase})) & 0xFF;
  };
  c.esp += 4;  // the callee's ret 4
  if (!CutsceneCamera()) {
    c.eax = run(1);
    return;
  }
  g_since_cut += RdF32(0x010FCB74);
  uint8_t* cam = GPtr(self);
  static uint8_t before[kCamSize], held[kCamSize];
  std::memcpy(before, cam, kCamSize);
  const uint32_t held_result = run(1);
  std::memcpy(held, cam, kCamSize);
  if (g_since_cut < 1.0 / 3.0) {
    c.eax = held_result;
    return;
  }
  std::memcpy(cam, before, kCamSize);
  const uint32_t free_result = run(0);
  if (Moved(cam, held, kCamFront, 1.0f / 30.0f) || Moved(cam, held, kCamPos, 1.0f / 3.0f)) {
    g_since_cut = 0;  // a cut: as held
    std::memcpy(cam, held, kCamSize);
    c.eax = held_result;
    return;
  }
  c.eax = free_result;
}

}  // namespace

// The update's "cutscene camera" checks: cmp [episode], 0; jle and then
// test al, al; je (mode 2) both passed.
void hook_00C9A84B(Ctx& c) { CmpFlags<4>(c, CutsceneCamera() ? 1 : Rd32(0x0112D754), 0); }
void hook_00C9A85E(Ctx& c) { TestFlags8(c, CutsceneCamera() ? 1 : uint8_t(c.eax)); }

// mov byte [esp+1C], 1: the hold flag.
void hook_00C9A68A(Ctx& c) { Wr8(c.esp + 0x1C, g_hold_at_cut); }

void hook_00A23628(Ctx& c) { UpdateFromPhase(c); }
void hook_00C9AB1B(Ctx& c) { UpdateFromPhase(c); }

// movss [edi+1B4], xmm1: the frame held at a cut.
namespace {
void StoreHeldFrame(Ctx& c) {
  if (!CutsceneCamera()) std::memcpy(GPtr(c.edi + 0x1B4), c.xmm[1].data(), 4);
}
}  // namespace
void hook_00C9A591(Ctx& c) { StoreHeldFrame(c); }
void hook_00C9A5D2(Ctx& c) { StoreHeldFrame(c); }

// The water's geometry is double buffered: the main thread fills one half
// while the render thread draws the other, and flips which is which when
// it is done (mov [01352E80], eax). It flips at once, while the render
// thread may still be drawing the last frame's water from the half that
// is now being refilled: the water flickers and tears. With the main loop
// not yet waiting for the render thread (CMainLoop::Synchronise's mode
// byte, [011F8FC8+FC8], 0), the flip is held until Synchronise has waited
// (008A187C) (FusionFix's water buffer fix, by its idea).
namespace {
bool g_water_flip_pending;
uint32_t g_water_flip_to;
bool WaterFlip() {
  static const bool on = OptionOn("GTAIV_WATER_FLIP", true);
  return on;
}
}  // namespace
void hook_009AD73B(Ctx& c) {
  if (WaterFlip() && Rd8(0x011F8FC8 + 0xFC8) == 0) {
    if (!g_water_flip_pending) {
      g_water_flip_to = c.eax;
      g_water_flip_pending = true;
    }
    return;
  }
  g_water_flip_pending = false;  // flipped now: a held flip is stale
  Wr32(0x01352E80, c.eax);
}
void hook_008A187C(Ctx& c) {  // cmp byte [018CACDD], 0
  if (g_water_flip_pending) {
    Wr32(0x01352E80, g_water_flip_to);
    g_water_flip_pending = false;
  }
  CmpFlags<1>(c, Rd8(0x018CACDD), 0);
}

// Minigames that only work at 30 fps (pool, air hockey, arm wrestling,
// bowling, drinking: their scripts count frames) get 30 fps while one is
// in progress. SET_MINIGAME_IN_PROGRESS(true) counts it in at [0120A680]
// (add [0120A680], 1, the calling script thread in eax, its name at
// +70); once a frame (the game's frame timing, 00855640, after its first
// call) the frame is held to 1/30 s while one of those is on
// (FusionFix's minigame frame limit, by its idea).
namespace {
bool g_minigame_capped;
}  // namespace
void hook_00B4F179(Ctx& c) {
  static const bool on = OptionOn("GTAIV_MINIGAME_30FPS", true);
  if (on && c.eax) {
    const char* p = reinterpret_cast<const char*>(GPtr(c.eax + 0x70));  // scrThread's program name
    std::string name(p, strnlen(p, 24));
    for (char& ch : name) ch = char(std::tolower(uint8_t(ch)));
    g_minigame_capped = name == "pool_game" || name == "air_hockey" || name == "arm_wrestling" ||
                        name == "tenpinbowl" || name == "drinking";
  }
  Wr32(0x0120A680, Rd32(0x0120A680) + 1);  // the flags of add are not used: ret follows
}
void hook_00855651(Ctx& c) {  // mov eax, [0112EAC0]
  using clock = std::chrono::steady_clock;
  static clock::time_point last;
  if (g_minigame_capped && Rd32(0x0120A680)) {
    const auto next = last + std::chrono::microseconds(33333);
    const auto now = clock::now();
    if (now < next) std::this_thread::sleep_until(next);
    last = std::max(next, now - std::chrono::milliseconds(100));  // no bursts after a long frame
  } else {
    last = clock::now();
  }
  c.eax = Rd32(0x0112EAC0);
}


// CRadarNY::UpdateTask: the radar is worked out only when 30 ms passed
// since the last time (cmp eax, 30; jbe): at high frame rates it moves in
// 30 Hz steps. Every frame instead, and its zoom, a step of the range a
// time (addss / subss xmm1, [k]), per time.
namespace {
bool RadarEveryFrame() {
  static const bool on = OptionOn("GTAIV_RADAR_EVERY_FRAME", true);
  return on;
}
float RadarZoomStep() { return RdF32(0x00E52BF8) * (RadarEveryFrame() ? StepsOf30() : 1.0f); }
}  // namespace
void hook_004B2C45(Ctx& c) { CmpFlags<4>(c, RadarEveryFrame() ? 31 : c.eax, 30); }
void hook_004B2CAA(Ctx& c) { SetLane0(c.xmm[1], Lane0(c.xmm[1]) + RadarZoomStep()); }
void hook_004B2CB7(Ctx& c) { SetLane0(c.xmm[1], Lane0(c.xmm[1]) - RadarZoomStep()); }

// CCutsceneManager::GetTimeStep (cdecl, float* time in ms, in and out):
// while a cutscene plays with audio (state 8 at [011515D0]) its time is
// the audio's play position (audCutsceneAudioEntity at 01160E60, 809EF0)
// less [01150468]. The audio thread moves that position in steps that do
// not fall on frames, so the animations judder at any frame rate. A clock
// of its own here: it runs with real time, is drawn toward the audio
// position (slowly for small differences, quicker for larger ones), never
// gets further ahead of it than one of its steps (the audio stalled) and
// never goes back, except when the audio jumps (a new section, a skip).
// Not in TBoGT (episode 2): its corrections for two cutscenes' drift are
// the game's; TLAD has none.
namespace {

struct CutsceneClock {
  bool locked = false;
  double clock = 0, last_now = 0, step = 0;
  int last_audio = -1;

  static double NowMs() {
    using clk = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clk::now().time_since_epoch()).count();
  }

  double Update(int audio) {
    const double now = NowMs(), frame = now - last_now;
    last_now = now;
    if (!locked) {
      locked = true;
      clock = audio;
      last_audio = audio;
      step = 0;
      return clock;
    }
    if (audio != last_audio) {  // how far the audio moves when it moves, averaged
      const double d = audio - last_audio;
      if (d > 0 && d < 250) step = step > 0 ? step + (d - step) * 0.1 : d;
      last_audio = audio;
    }
    const double off = audio - clock;
    if (frame < 0 || frame > 250 || std::fabs(off) > 250) {  // a jump, or a long frame
      clock = std::fabs(off) > 250 ? double(audio) : std::max(clock, double(audio));
      return clock;
    }
    double next = clock + frame;
    const double behind = audio - next;
    next += behind * std::min(1.0, frame / (std::fabs(behind) < 30 ? 500.0 : 100.0));
    next = std::min(next, audio + std::max(step, 10.0) + 5);  // the audio stalled: wait for it
    clock = std::max(clock, next);
    return clock;
  }
};

}  // namespace

void f_007F16B0(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CUTSCENE_CLOCK", true);
  if (!on || Rd32(0x0112D754) == 2) return f_007F16B0_lifted(c);  // not in TBoGT
  static CutsceneClock clock;
  const uint32_t out = Rd32(c.esp + 4);
  const float in = RdF32(out);
  const uint32_t self = c.ecx;
  c.ecx = 0x01160E60;
  const int32_t play = int32_t(CallGuest(c, 0x00809EF0, {}));  // the audio's position, ms (or -1)
  c.ecx = self;
  float t;
  if (Rd32(0x011515D0) != 8) {  // not playing yet, or between sections: the game's step
    clock.locked = false;
    t = RdF32(0x010FCB74) * 1000.0f + in;
  } else if (play == -1) {  // no audio: the unscaled frame time
    clock.locked = false;
    t = RdF32(0x010FCB84) * 1000.0f + in;
  } else {
    t = std::max(0.0f, float(clock.Update(std::max(play, 0)) - RdF32(0x01150468)));
  }
  WrF32(out, t);
  c.esp += 4;  // ret
}

// CHeli::ApplyCollisionInternal: the damage a collision does to the main
// rotor, the tail rotor and the tail is a per-frame amount (x87: fmul by a
// constant, or by the one already on the stack): at 120 fps they broke
// off four times sooner. Scaled by the step against 1/30 s.
namespace {
bool HeliRotorDamage() {
  static const bool on = OptionOn("GTAIV_HELI_ROTOR_DAMAGE", true);
  return on;
}
float RotorScale() { return HeliRotorDamage() ? StepsOf30() : 1.0f; }
uint32_t FBits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}
}  // namespace
void hook_009C9CD0(Ctx& c) {  // fmul [k]: main rotor
  x87::ArithF32(c.fpu, x87::Op::Mul, FBits(RdF32(0x00D95B68) * RotorScale()));
}
void hook_009C9E03(Ctx& c) {  // fmul [k]: tail
  x87::ArithF32(c.fpu, x87::Op::Mul, FBits(RdF32(0x00D7A9C8) * RotorScale()));
}
void hook_009C9F14(Ctx& c) {  // fmulp st(1), st; fld [esp+14]: tail rotor
  x87::ArithF32(c.fpu, x87::Op::Mul, FBits(RotorScale()));
  x87::ArithStiSt0(c.fpu, x87::Op::Mul, 1);
  x87::Pop(c.fpu);
  x87::LoadF32(c.fpu, Rd32(c.esp + 0x14));
}

// CLoadingScreens::RenderSegmentSprites: the pictures' movement is timed
// by a clock of 66.6 ms steps (movss xmm2, [00EDF920]), made for the PC's
// 64 fps limit on loading screens: twice the speed they have on the PS3
// at 30 fps. 33.3 ms.
namespace {
float LoadingClock() {
  static const bool on = OptionOn("GTAIV_LOADING_ANIMATION", true);
  return on ? 33.3f : RdF32(0x00EDF920);
}
}  // namespace
void hook_004A1450(Ctx& c) { LoadSs(c.xmm[2], LoadingClock()); }
void hook_004A14A8(Ctx& c) { LoadSs(c.xmm[2], LoadingClock()); }

// CCamFollowVehicle's sprung mounting (00A7CFC0, thiscall: vehicle,
// offset, float* angles out, scale; ret 10h): the bonnet / bumper
// cameras sway on a spring stepped by the frame's time step ([010FCB74],
// and its inverse [010FCB78]), its offsets at +2C0..2C8 added to the
// angles times the scale. A spring stepped by varying, short steps
// jitters at high frame rates. The game's own spring is run here in
// fixed 1/30 s steps (the two reads of the step hooked while it runs),
// its output into a scratch buffer, and the offsets between the last two
// steps interpolated into the angles (FusionFix's sprung mounting, by
// its idea).
namespace {
bool g_spring_fixed_step;
struct Spring {
  float due = 0, old[3] = {0, 0, 0};
  uint32_t last[3] = {0, 0, 0};  // the offsets as the last call left them
  bool started = false;
};
}  // namespace
void hook_00A7CFC9(Ctx& c) { LoadSs(c.xmm[0], g_spring_fixed_step ? 1.0f / 30.0f : RdF32(0x010FCB74)); }
void hook_00A7D08A(Ctx& c) { LoadSs(c.xmm[3], g_spring_fixed_step ? 30.0f : RdF32(0x010FCB78)); }

void f_00A7CFC0(Ctx& c) {
  static const bool on = OptionOn("GTAIV_HOOD_CAMERA", true);
  if (!on) return f_00A7CFC0_lifted(c);
  static std::unordered_map<uint32_t, Spring> springs;  // by camera (main thread only)
  const uint32_t self = c.ecx, veh = Rd32(c.esp + 4), off = Rd32(c.esp + 8), out = Rd32(c.esp + 12);
  const uint32_t scale = Rd32(c.esp + 16);
  Spring& sp = springs[self];
  auto pos = [&](int i) { return RdF32(self + 0x2C0 + 4 * uint32_t(i)); };
  // The offsets change only in the steps below; changed in between, the
  // spring was reset (a new camera at this address, 00A7F470 zeroes it):
  // started over from there, not from the last camera's sway.
  bool reset = !sp.started;
  for (int i = 0; i < 3; i++) reset |= Rd32(self + 0x2C0 + 4 * uint32_t(i)) != sp.last[i];
  if (reset) {
    sp.started = true;
    sp.due = 0;
    for (int i = 0; i < 3; i++) sp.old[i] = pos(i);
  }
  sp.due += RdF32(0x010FCB74);
  int runs = 0;
  const uint32_t esp = c.esp;
  for (; sp.due >= 1.0f / 30.0f && runs < 4; runs++) {
    sp.due -= 1.0f / 30.0f;
    for (int i = 0; i < 3; i++) sp.old[i] = pos(i);
    c.esp -= 48;  // the scratch angles, then the arguments and a return address
    const uint32_t scratch = c.esp + 32;
    std::memset(GPtr(scratch), 0, 16);
    c.esp -= 20;
    Wr32(c.esp, 0);
    Wr32(c.esp + 4, veh);
    Wr32(c.esp + 8, off);
    Wr32(c.esp + 12, scratch);
    Wr32(c.esp + 16, scale);
    c.ecx = self;
    g_spring_fixed_step = true;
    f_00A7CFC0_lifted(c);  // its ret 10h pops the arguments and the return address
    g_spring_fixed_step = false;
    c.esp = esp;
  }
  for (int i = 0; i < 3; i++) sp.last[i] = Rd32(self + 0x2C0 + 4 * uint32_t(i));
  if (sp.due > 1.0f / 30.0f) sp.due = 0;  // after a long frame: no catching up beyond that
  const float t = sp.due * 30.0f;
  float k;
  std::memcpy(&k, &scale, 4);
  for (int i = 0; i < 3; i++) {
    const float v = sp.old[i] + (pos(i) - sp.old[i]) * t;
    WrF32(out + 4 * uint32_t(i), RdF32(out + 4 * uint32_t(i)) + v * k);
  }
  c.esp += 4 + 16;  // ret 10h
}

}  // namespace recomp::gen
