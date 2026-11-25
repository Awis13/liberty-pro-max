// Ray-traced sun shadows: only the far cascade's casters are drawn.
//
// With traced shadows the deferred lighting and the forward shaders read
// the traced shadow, not the game's cascaded shadow maps; the cascades are
// still drawn because their casters (what the camera does not see) feed the
// traced scene (shadows_lights.cpp). A pixel shader that still samples the
// maps (none of the game's own) reads one that is lit everywhere instead
// (GTAIV_RT_GAME_SHADOWS=lit by default unless the mode is All:
// rt_cascades.h, GameShadowsDefault; main.cpp). The four cascades draw much
// the same casters four times over: two thirds of a frame's draws on a busy
// street, each noted for the traced scene, on the thread that limits the
// frame rate. The far cascade covers the others' ground, so the three
// nearer ones' lists are left out: the cascade phase (008CBF50) sets the
// cascade's bit in [012284C0] and draws its list with the call at 008CC106
// (cdecl: skipping it is doing nothing). What a nearer cascade alone would
// have drawn (small props the far one leaves out) is in the scene only
// while it is retained after the camera saw it: until it has gone
// retain_frames presented frames without a draw (900), or the camera's view
// has held it undrawn for kInViewFrames, or it is replaced or let go for
// memory (x86recomp rt_scene.h).
//
// And the far one only on two runs of the cascade phase in sixteen (a pair
// of consecutive frames, counted by the host), without what moves: the
// scene retains what stands still (an instance listed two presented frames
// running at one place is kept, and let go after 30 frames in the camera's
// view undrawn, more than the 14 between two pairs), so the buildings and
// trees behind the camera stay in it between the pairs. What moves would
// come and go with the pairs, its shadow blinking, so it is left out of
// that draw altogether: the far cascade's list is drawn by the game's own
// call (its static lists, trees included, as the game draws a cascade),
// with the entity lists 0 and 1 (vehicles, people, dynamic objects) not
// drawn in any pass: 00959FF0, which draws one entity list of one pass,
// returns at once for them during that draw (rt_cascades.h has what
// 0095AC40 draws and why not the way it draws a local light's list). A
// car or person the camera sees is in the scene by the main view's draws
// all the same; one it does not see casts no shadow into view.
// GTAIV_RT_CASCADES=all draws the four as before, =far the far one every
// frame with everything; GTAIV_RT_CASCADE_HYBRID=1 makes it all whatever
// GTAIV_RT_CASCADES says (rt_cascades.h: EffectiveMode and the decision,
// tested).
//
// And a nearer cascade is not redrawn empty on every run either
// (GTAIV_RT_CASCADE_CLEAR_SKIP, on unless "0"): with its list skipped the
// phase still enqueued the clear of the cascade map (a 4096 x 4096 pass)
// and its copy into the cascade's quarter of the atlas (a pass on the
// 8192 x 2048 atlas), the same texels written every frame, 0.85-1.1 ms of
// GPU for the three. The two enqueues are hooked (008CC0F7, 008CC1BD) and
// left out while the quarter holds that result; on the first two runs,
// after the targets are made anew or the device is reset, on every 128th
// run, and whenever the list is drawn, both are enqueued as the game does
// (rt_cascades.h: EmptyCascades, tested). The far cascade's turn is the
// game's.

#include "rt_cascades.h"

#include <atomic>
#include <cstdlib>

#include "hook_util.h"
#include "option.h"
#include "runtime/gfx/device.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;
namespace rc = gtaiv_rt_cascades;

void f_00B6FBF0(Ctx& c);
void f_00959FF0_lifted(Ctx& c);
void f_004CE610(Ctx& c);
void f_008CAD60(Ctx& c);
void f_008CB160_lifted(Ctx& c);

namespace {

// The cascade being drawn: 1 << its index, set by the phase at 008CC046,
// before the three hooked sites of the cascade's turn, and by nothing else.
constexpr uint32_t kCascadeBit = 0x012284C0;

thread_local bool t_far_without_dynamic = false;

// The cascades' device resets (f_008CB160), counted on the thread that
// resets the device and read on the cascade phase's.
std::atomic<uint32_t> g_device_resets{0};

rc::Mode ModeOf() {
  static const rc::Mode m = rc::EffectiveMode(std::getenv("GTAIV_RT_CASCADES"), std::getenv("GTAIV_RT_CASCADE_HYBRID"));
  return m;
}

// GTAIV_RT_CASCADE_CLEAR_SKIP=0: every cascade's clear and copy enqueued on
// every run, as the game does.
bool ClearSkipOn() {
  static const bool on = OptionOn("GTAIV_RT_CASCADE_CLEAR_SKIP", true);
  return on;
}

// The nearer cascades' turns (the cascade phase runs on one thread).
rc::EmptyCascades& Empty() {
  static rc::EmptyCascades e;
  return e;
}

rc::CascadeTargets TargetsNow() {
  rc::CascadeTargets t;
  t.word[0] = Rd32(0x012284B0);  // the cascade map
  t.word[1] = Rd32(0x012284A8);  // its depth
  t.word[2] = Rd32(0x012284AC);  // the atlas
  t.word[3] = Rd32(0x0122845C);  // 008C8910's three arguments the targets were made with
  t.word[4] = Rd32(0x01228448);
  t.word[5] = Rd32(0x01228450);
  t.word[6] = g_device_resets.load(std::memory_order_acquire);
  return t;
}

}  // namespace

// The call 004CE610(008C6AF0) (cdecl; pushed: 008C6AF0), which enqueues the
// clear of the cascade map, the first of a cascade's turn: not made while a
// nearer cascade's quarter of the atlas holds the empty result (the pushed
// argument stays for the caller's add esp, as after the call).
void hook_008CC0F7(Ctx& c) {
  if (!Empty().Begin(ClearSkipOn(), ModeOf(), recomp::gfx::RayTracedShadows(), Rd32(kCascadeBit), TargetsNow())) return;
  Push(c, 0x008CC0FC);
  f_004CE610(c);
}

// The call 00B6FBF0(list, -1, ebx) (cdecl; pushed: ebx, -1, list).
void hook_008CC106(Ctx& c) {
  // The far cascade's turns (the cascade phase runs on one thread).
  static rc::SparseSchedule far;
  // A cascade whose clear was left out: nothing drawn onto the map it did
  // not clear (it holds the far cascade), even if ray-traced shadows went
  // off since the clear's site.
  if (Empty().LeftOut(Rd32(kCascadeBit))) return;
  const rc::Decision d = rc::Decide(ModeOf(), recomp::gfx::RayTracedShadows(), Rd32(kCascadeBit), far);
  Empty().Listed(Rd32(kCascadeBit), d);
  if (d == rc::Decision::Skip) return;
  // The game's call; for FarWithoutDynamic with f_00959FF0 leaving out the
  // entity lists 0 and 1 meanwhile.
  struct Drawing {
    explicit Drawing(bool on) { t_far_without_dynamic = on; }
    ~Drawing() { t_far_without_dynamic = false; }
  } drawing(d == rc::Decision::FarWithoutDynamic);
  Push(c, 0x008CC10B);
  f_00B6FBF0(c);
}

// The call 008CAD60(008C8B10, &cascade) (cdecl; pushed: &cascade,
// 008C8B10), which enqueues the map's copy into the cascade's quarter of
// the atlas, the last of a cascade's turn: made exactly when the clear was.
void hook_008CC1BD(Ctx& c) {
  if (!Empty().End(Rd32(kCascadeBit))) return;
  Push(c, 0x008CC1C2);
  f_008CAD60(c);
}

// 008CB160 (no arguments), the cascades' callback after a device reset
// (008CBCF0 registers it with 008C7470, the one before the reset; it has
// 008C8910 make the targets anew where their size changed): the targets'
// textures may be new ones behind the same objects, so the count
// CascadeTargets carries moves and the nearer cascades are drawn in full
// again.
void f_008CB160(Ctx& c) {
  g_device_resets.fetch_add(1, std::memory_order_release);
  f_008CB160_lifted(c);
}

// 00959FF0 (cdecl: render list, entity list, mode, pass, 0095AC40's second
// argument), one entity list of one pass of a shadow map's draw: nothing
// for the entity lists rt_cascades.h leaves out of the far cascade, while
// it is drawn so; else the game's.
void f_00959FF0(Ctx& c) {
  if (t_far_without_dynamic && rc::SuppressedList(Rd32(c.esp + 8))) {
    c.esp += 4;  // cdecl: the return address only
    return;
  }
  f_00959FF0_lifted(c);
}

}  // namespace recomp::gen
