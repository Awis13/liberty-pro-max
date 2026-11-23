// Emergency light bars as a light a bulb (siren_lights.h): the hooks.
//
// 008FABD0 (the six siren functions' caller) and 008FA670 run with a mark
// on their thread; under it 00A6F7B0 notes each corona, and the one
// 00A2E5F0 that follows adds a spot light a turning bulb through the game's
// own 00A2E5F0 (so they are lights of the frame's list like any other:
// rt_lights.cpp hands them to the renderer, the forward shaders get them),
// in place of the light above the roof. Only while rays light the scene
// (gfx::RayTracedShadows), and not with GTAIV_SIREN_BULBS=0.
// GTAIV_SIREN_LIGHT=intensity,radius,inner,outer,down changes what a bulb's
// light is made with (to tune it by eye).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hook_util.h"
#include "runtime/gfx/device.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"
#include "siren_lights.h"

namespace recomp::gen {

using namespace hook;
namespace sl = gtaiv_siren;

void f_008FABD0_lifted(Ctx& c);
void f_008FA670_lifted(Ctx& c);
void f_00A6F7B0_lifted(Ctx& c);
void f_00A2E5F0_lifted(Ctx& c);

namespace {

thread_local int t_siren = 0;  // inside a siren function
thread_local sl::Bulb t_bulbs[sl::kMaxBulbs];
thread_local int t_count = 0;
thread_local bool t_flashing = false;  // a corona without an axis was drawn

bool On() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_SIREN_BULBS");
    return !e || std::atoi(e) != 0;
  }();
  return on && recomp::gfx::RayTracedShadows();
}

const sl::Tuning& Tuned() {
  static const sl::Tuning t = [] {
    sl::Tuning v;
    if (const char* e = std::getenv("GTAIV_SIREN_LIGHT")) {
      sl::Tuning r = v;
      if (std::sscanf(e, "%f,%f,%f,%f,%f", &r.intensity, &r.radius, &r.inner, &r.outer, &r.down) >= 1 &&
          r.intensity >= 0.0f &&
          r.radius > 0.0f && r.inner >= 0.0f && r.outer >= r.inner)
        v = r;
    }
    return v;
  }();
  return t;
}

struct InSiren {
  InSiren() {
    if (t_siren++ == 0) {
      t_count = 0;
      t_flashing = false;
    }
  }
  ~InSiren() { --t_siren; }
};

void ReadVec(uint32_t va, float out[3]) {
  for (int k = 0; k < 3; k++) out[k] = RdF32(va + 4u * uint32_t(k));
}

}  // namespace

void f_008FABD0(Ctx& c) {
  InSiren in;
  f_008FABD0_lifted(c);
}

void f_008FA670(Ctx& c) {
  InSiren in;
  f_008FA670_lifted(c);
}

// 00A6F7B0(id, r, g, b, intensity, position*, size, ..., axis* (the 14th), 0):
// the game's, a siren's noted first.
void f_00A6F7B0(Ctx& c) {
  if (t_siren && On()) {
    const uint32_t pos = Rd32(c.esp + 0x18), axis = Rd32(c.esp + 0x38);
    if (!axis) {
      t_flashing = true;
    } else if (pos && t_count < sl::kMaxBulbs) {
      sl::Bulb& b = t_bulbs[t_count++];
      b.r = uint8_t(Rd32(c.esp + 0x08));
      b.g = uint8_t(Rd32(c.esp + 0x0C));
      b.b = uint8_t(Rd32(c.esp + 0x10));
      ReadVec(pos, b.pos);
      ReadVec(axis, b.axis);
      b.has_axis = true;
    }
  }
  f_00A6F7B0_lifted(c);
}

// 00A2E5F0(0, type, flags, direction*, tangent*, position*, colour*,
// intensity, 0, texture, radius, inner, outer, id, room, 0): a siren's light
// above the roof becomes its turning bulbs' lights.
void f_00A2E5F0(Ctx& c) {
  if (!t_siren || !On() || t_count == 0) {
    f_00A2E5F0_lifted(c);
    return;
  }
  // Beams only for the sirens near the camera, kBeamCars of them a frame
  // (frames told apart by the game's clock, which the sirens' phase reads).
  {
    constexpr uint32_t kViewport = 0x011F8290, kClock = 0x010FCB6C;
    static thread_local uint32_t frame_clock = 0;
    static thread_local int frame_cars = 0;
    const uint32_t now = Rd32(kClock);
    if (now != frame_clock) {
      frame_clock = now;
      frame_cars = 0;
    }
    const uint32_t vp = Rd32(kViewport);
    float cam[3] = {};
    if (vp) ReadVec(vp + 0x10 + 0x70, cam);
    if (!vp || !sl::WithinReach(cam, t_bulbs[0].pos) || frame_cars >= sl::kBeamCars) {
      t_count = 0;
      f_00A2E5F0_lifted(c);
      return;
    }
    frame_cars++;
  }
  constexpr int kArgs = 16;
  const uint32_t frame = c.esp;  // the return address, then the arguments
  // GTAIV_SIREN_DEBUG=1: the bulbs of every 30th siren light, said.
  static const bool debug = std::getenv("GTAIV_SIREN_DEBUG") != nullptr;
  static thread_local unsigned calls = 0;
  if (debug && calls++ % 30 == 0) {
    std::fprintf(stderr, "siren: %d turning bulbs%s:", t_count, t_flashing ? " (and flashing ones)" : "");
    for (int k = 0; k < t_count; k++)
      std::fprintf(stderr, " [%u,%u,%u at %.1f %.1f %.1f axis %.2f %.2f %.2f]", t_bulbs[k].r, t_bulbs[k].g, t_bulbs[k].b,
                   t_bulbs[k].pos[0], t_bulbs[k].pos[1], t_bulbs[k].pos[2], t_bulbs[k].axis[0], t_bulbs[k].axis[1],
                   t_bulbs[k].axis[2]);
    std::fprintf(stderr, "\n");
  }
  uint32_t words[1 + kArgs];
  for (int i = 0; i <= kArgs; i++) words[i] = Rd32(frame + 4u * uint32_t(i));
  // Below the caller's frame: the four vectors of a light, then its call.
  const uint32_t vecs = frame - 0x40;
  const uint32_t top = vecs - 0x10;
  uint32_t popped = 4;  // what a return takes off (found from the first call)
  int made = 0;
  bool lit[sl::kMaxBulbs];
  sl::Pick(t_bulbs, t_count, lit);
  for (int k = 0; k < t_count; k++) {
    sl::Spot s;
    if (!lit[k] || !sl::SpotOf(t_bulbs[k], Tuned(), &s)) continue;
    for (int j = 0; j < 3; j++) {
      WrF32(vecs + 4u * uint32_t(j), s.dir[j]);
      WrF32(vecs + 0x10 + 4u * uint32_t(j), s.tangent[j]);
      WrF32(vecs + 0x20 + 4u * uint32_t(j), s.pos[j]);
      WrF32(vecs + 0x30 + 4u * uint32_t(j), s.colour[j]);
    }
    for (uint32_t pad : {0x0Cu, 0x1Cu, 0x2Cu, 0x3Cu}) Wr32(vecs + pad, 0);
    uint32_t a[1 + kArgs];
    std::memcpy(a, words, sizeof a);
    a[2] = 2;  // a spot
    // (With ray-traced lights a beam is shadowed by rays: flag 4, dynamic
    // shadows, which the bulb's own light does not carry.)
    if (recomp::gfx::RayTracedShadows()) a[3] |= 4u;
    a[4] = vecs;
    a[5] = vecs + 0x10;
    a[6] = vecs + 0x20;
    a[7] = vecs + 0x30;
    a[8] = Bits(s.intensity);
    a[11] = Bits(s.radius);
    a[12] = Bits(s.inner);
    a[13] = Bits(s.outer);
    c.esp = top;
    for (int i = kArgs; i >= 0; i--) Push(c, a[i]);
    const uint32_t before = c.esp;
    f_00A2E5F0_lifted(c);
    popped = c.esp - before;
    made++;
  }
  t_count = 0;
  c.esp = frame;
  if (made == 0 || t_flashing) {
    // No turning bulb made a light, or some bulbs only flash: the game's light.
    f_00A2E5F0_lifted(c);
    return;
  }
  c.esp = frame + popped;  // as the game's call would have returned
}

}  // namespace recomp::gen
