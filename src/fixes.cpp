// Fixes of the game's own bugs (each as small as the bug).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "runtime/lifted.h"
#include "runtime/runtime.h"
#include "runtime/x87.h"

namespace recomp::gen {

void f_008CC970_lifted(Ctx& c);
void f_0094A6D0_lifted(Ctx& c);
void f_0094AC20_lifted(Ctx& c);
void f_00A7C8E0_lifted(Ctx& c);
void f_009AE2C0_lifted(Ctx& c);
void f_009AE360_lifted(Ctx& c);

// The sweep of a pool of objects (cdecl, keep-all flag) that ends it at
// shutdown (008CCA80) reads each object's model's streaming flags
// (008B4C20) from the streaming table at [00F493F0]. Quitting before the
// world was ever loaded (from the menus or the intro), there is no table:
// the read faults (guest 000000B6). Then there is nothing streamed to
// release either: the sweep is skipped.
void f_008CC970(Ctx& c) {
  if (!Rd32(0x00F493F0)) {
    c.esp += 4;  // ret
    return;
  }
  f_008CC970_lifted(c);
}

// How pinned down a ped is (0-100, from being shot at) goes down each
// frame by the time step * 30 (thiscall, float amount; CPedIntelligence),
// cut to a whole number: above 30 fps that is 0, it never goes down, and
// peds once shot at stay ducked in cover for good. The fraction is kept
// per ped and taken off in whole points as they add up.
void f_0094A6D0(Ctx& c) {
  static std::mutex mu;
  static std::unordered_map<uint32_t, float> left;  // by CPedIntelligence
  float amount;
  const uint32_t at = c.esp + 4;
  uint32_t bits = Rd32(at);
  std::memcpy(&amount, &bits, 4);
  {
    std::lock_guard<std::mutex> l(mu);
    float& acc = left[c.ecx];
    acc += amount;
    const float whole = float(int(acc));
    acc -= whole;
    amount = whole;
  }
  std::memcpy(&bits, &amount, 4);
  Wr32(at, bits);
  f_0094A6D0_lifted(c);
}

// A ped's task attempt counter goes up once a frame (thiscall;
// CPedIntelligence::ProcessStaticCounter); tasks give up when it reaches
// 30, meant as a second. At 120 fps that is a quarter second: peds and
// drivers abandon what they are doing (paths, getting around things)
// early. Run at 30 Hz from a per-ped accumulator of the time step.
void f_0094AC20(Ctx& c) {
  static std::mutex mu;
  static std::unordered_map<uint32_t, float> due;  // by CPedIntelligence
  uint32_t step_bits = Rd32(0x010FCB74);  // CTimer::ms_fTimeStep
  float step;
  std::memcpy(&step, &step_bits, 4);
  int runs = 0;
  {
    std::lock_guard<std::mutex> l(mu);
    float& t = due[c.ecx];
    t += step;
    for (; t >= 1.0f / 30.0f && runs < 4; runs++) t -= 1.0f / 30.0f;
    if (t > 1.0f / 30.0f) t = 0;  // after a long frame: no catching up beyond that
  }
  if (!runs) {
    c.esp += 4;  // ret
    return;
  }
  const uint32_t self = c.ecx;
  for (int i = 1; i < runs; i++) {
    c.ecx = self;
    c.esp -= 4;  // a return address for its ret to pop
    Wr32(c.esp, 0);
    f_0094AC20_lifted(c);
  }
  c.ecx = self;
  f_0094AC20_lifted(c);
}

// CTimer's millisecond clocks (CTimer::Update) and the scripts' timers
// (rage::scrThread::Update: TIMERA, TIMERB, WAIT) advance by the frame's
// time step in whole milliseconds, the fraction dropped: at 144 fps
// 6.94 ms count as 6, so they run 14% slower than real time and than the
// animations, physics and audio (4% at 60 or 120). A screen fade then
// outlasts the cutscene it belongs to (Off Route's script finds the
// screen not yet faded out when its cutscene ends, never fades it back
// in: the "infinite loading" screen). Each clock's fraction is carried to
// its next step; the scripts' once a frame, the same for every script.
namespace {

float g_clock_left[3], g_script_left;
uint32_t g_script_frame = ~0u;
int32_t g_script_carry;

float F32(uint32_t va) {
  const uint32_t bits = Rd32(va);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// The whole milliseconds to add for a step of `whole` (eax) when the exact
// step is `ms`: whole plus the carried fraction's whole part. A step that
// is not `ms` within a millisecond came from elsewhere: added as it is.
int32_t Carry(float& left, float ms, int32_t whole) {
  if (!(ms - float(whole) < 1.0f && float(whole) - ms < 1.0f)) return whole;
  left += ms - float(whole);
  const int32_t extra = int32_t(left);
  left -= float(extra);
  return whole + extra;
}

// add [clock], eax; the clock's step in seconds is at clock + 8 by then.
void AddClock(Ctx& c, uint32_t clock, int i) {
  const int32_t add = Carry(g_clock_left[i], F32(clock + 8) * 1000.0f, int32_t(c.eax));
  Wr32(clock, Rd32(clock) + uint32_t(add));
}

}  // namespace

void hook_007CC06F(Ctx& c) { AddClock(c, 0x010FCB6C, 0); }  // the game's clock
void hook_007CC1BB(Ctx& c) { AddClock(c, 0x010FCBEC, 1); }
void hook_007CC33E(Ctx& c) { AddClock(c, 0x010FCC1C, 2); }

// add [ecx+1C/20/24], eax with eax = (int)(xmm0 = time step * 1000).
void hook_00625414(Ctx& c) {
  const uint32_t frame = Rd32(0x010FCC30);  // CTimer's frame counter
  if (frame != g_script_frame) {
    g_script_frame = frame;
    float ms;
    std::memcpy(&ms, c.xmm[0].data(), 4);
    g_script_carry = Carry(g_script_left, ms, int32_t(c.eax)) - int32_t(c.eax);
  }
  c.eax += uint32_t(g_script_carry);
  for (uint32_t off : {0x1Cu, 0x20u, 0x24u}) Wr32(c.ecx + off, Rd32(c.ecx + off) + c.eax);
}

// Per-frame amounts made per time: the steering bias while drunk is added
// once a frame (twice as strong at 60 fps as at 30), and the aim zoom's
// smoothing takes a fixed share a frame (twice as fast at 60). Scaled by
// the step against 1/30 s; a share k a frame becomes 1 - (1 - k)^(30 step).
namespace {

float StepsOf30() {
  uint32_t bits = Rd32(0x010FCB74);  // CTimer::ms_fTimeStep
  float step;
  std::memcpy(&step, &bits, 4);
  return step * 30.0f;
}

float& Lane0(Ctx& c, int reg) { return *reinterpret_cast<float*>(c.xmm[reg].data()); }

// The float an instruction's [disp32] operand (at va + 4) reads.
float Operand(uint32_t va) {
  uint32_t bits = Rd32(Rd32(va + 4));
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

float PerFrameShare(float k) { return 1.0f - std::pow(1.0f - k, StepsOf30()); }

}  // namespace

// CAutomobile::ProcessControlInputs: addss xmm0, [esi+10D4] (drunk bias).
void hook_00A53E4E(Ctx& c) {
  uint32_t bits = Rd32(c.esi + 0x10D4);
  float bias;
  std::memcpy(&bias, &bits, 4);
  Lane0(c, 0) += bias * StepsOf30();
}

// CBike::ProcessControlInputs: mulss xmm0, [k] (drunk bias).
void hook_00B9A94F(Ctx& c) { Lane0(c, 0) *= Operand(0x00B9A94F) * StepsOf30(); }

// CCamAimWeapon::AimFree, weapons: movss xmm2, [k] (zoom share a frame).
void hook_00A9C466(Ctx& c) { Lane0(c, 2) = PerFrameShare(Operand(0x00A9C466)); }

// CCamAimWeapon::AimFree, melee: mulss xmm0, [k].
void hook_00A98D7B(Ctx& c) { Lane0(c, 0) *= PerFrameShare(Operand(0x00A98D7B)); }

// CCamFollowVehicle::ProcessHandBrakeSwing (thiscall, 2 args, double in
// st0): the swing the follow camera adds each frame while the handbrake
// turns the car, made for 30 fps. At 120 fps it is added four times as
// often and the camera swings round the car, several turns in a fast
// handbrake turn. Scaled by the frame's share of a 30 fps frame (as
// FusionFix does).
void f_00A7C8E0(Ctx& c) {
  f_00A7C8E0_lifted(c);
  const float k = StepsOf30();
  uint32_t bits;
  std::memcpy(&bits, &k, 4);
  x87::ArithF32(c.fpu, x87::Op::Mul, bits);
}

// CCamFollowVehicle auto-centring: two divisors clamped from below
// (comiss; ja/jbe; movaps), which keeps the centring from scaling with the
// frame rate. The clamps are skipped, as FusionFix does; the flags they
// set are dead (the next compare or test sets its own).
void hook_00A810F4(Ctx& c) { c.xmm[2] = c.xmm[3]; }
void hook_00A813DC(Ctx& c) { c.xmm[1] = c.xmm[7]; }

// What animates or recurs by the frame counter ([010FCC30]) runs as fast
// as the frame rate: at 120 fps four times the speed it has at 30.
namespace {

// A frame counter at 30 Hz of real time.
uint32_t FramesAt30() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  return uint32_t(std::chrono::duration<double>(clock::now() - start).count() * 30.0);
}

// div ecx where ecx is a period in frames (something every n frames): the
// period stretched to the frame rate, so as many come a second as at 30.
void PeriodDiv(Ctx& c) {
  const float f = std::min(StepsOf30(), 1.0f);
  uint32_t d = c.ecx;
  if (f > 0) d = uint32_t(float(d) / f);
  d = std::max<uint32_t>(d, 1);
  const uint64_t n = (uint64_t(c.edx) << 32) | c.eax;
  c.eax = uint32_t(n / d);
  c.edx = uint32_t(n % d);
}

// cdecl float argument at [esp + off] scaled by the step against 1/30 s.
void ScaleArg(Ctx& c, uint32_t off) {
  uint32_t bits = Rd32(c.esp + off);
  float v;
  std::memcpy(&v, &bits, 4);
  v *= StepsOf30();
  std::memcpy(&bits, &v, 4);
  Wr32(c.esp + off, bits);
}

}  // namespace

void hook_004AF577(Ctx& c) { c.eax = FramesAt30(); }  // the chase blips' flashing
void hook_008ACE69(Ctx& c) { c.ecx = FramesAt30(); }  // the saving/loading spinner
void hook_009C8239(Ctx& c) { c.edx = FramesAt30(); }  // helicopters' blinking lights

// CHeli::PreRender: the rotors spin by a per-frame amount (mulss by [k]).
void hook_009CB59C(Ctx& c) { Lane0(c, 2) *= Operand(0x009CB59C) * StepsOf30(); }  // main rotor
void hook_009CB5DA(Ctx& c) { Lane0(c, 1) *= Operand(0x009CB5DA) * StepsOf30(); }  // tail rotor

void hook_00AB8DB7(Ctx& c) { PeriodDiv(c); }  // helicopter downwash: wind particles
void hook_00AF1FCC(Ctx& c) { PeriodDiv(c); }  // boats' wake ripples
void hook_00B91371(Ctx& c) { PeriodDiv(c); }  // swimmers' splash ripples

// Water pushed per frame (cdecl): by a helicopter's downwash (speed, the
// third argument) and by what floats (the fourth).
void f_009AE2C0(Ctx& c) {
  ScaleArg(c, 0x0C);
  f_009AE2C0_lifted(c);
}
void f_009AE360(Ctx& c) {
  ScaleArg(c, 0x10);
  f_009AE360_lifted(c);
}

}  // namespace recomp::gen
