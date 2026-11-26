// What the game culls while shadows are ray traced (native.cpp, the hooks;
// tests/rt_anticull_test.cpp): the ray-traced scene is what the game draws
// in a frame (and what x86recomp keeps of earlier frames), so whatever
// casts a shadow into the picture has to be drawn even where the camera
// does not see it. As gta4-rtx (github.com/xoxor4d/gta4-rtx, gta4.cpp
// frustum_planes_check_hk and static_world_culling_verdict: ideas only, no
// licence) does for the Complete Edition.
//
// What it takes (2026-11-11, gtaiv-lab rt_turn.py: a camera turned round
// one ground point, 72 steps, the sun's shadow there against the game's own
// without rays): the near rule and the static rule on every test of
// 004151B0 for a perspective viewport. A building's verdict (00957450) was
// already "drawn" where its shadow went missing: its drawable culls its own
// pieces outside the view through 004151B0 again. The map sections around
// the camera (MarkSquare) and the rule in 00A1DC10 changed nothing there
// and cost some 6 fps: off unless GTAIV_RT_ANTICULL_SECTIONS=1 /
// GTAIV_RT_ANTICULL_OCCLUDER=1. GTAIV_RT_ANTICULL=0: none of it;
// GTAIV_RT_TRACE_AT=x,y,z: the tests of spheres holding that point, said
// every 2 s.
//
// Near: the game's sphere-against-frustum test, 004151B0 (CE 0x431E40),
// thiscall on a rage::grcViewport (ecx) with the sphere's centre x, y, z,
// its radius and an optional float* (stack +4 .. +0x14, ret 0x14): planes
// at +0x300 (six of a, b, c, d: a x + b y + c z + d + r < 0 is outside);
// with the pointer it writes plane 0's value there and answers 1 inside,
// 0 outside; without it 2 wholly inside, 1 crossing, 0 outside. Its
// callers: entities (00904D10: people, vehicles, objects), lights
// (00A2DF00, 00A2C680) and more, 32 in all. A sphere whose centre lies
// within kNearAll of the viewport's camera is inside (1 or 2, as above),
// for a perspective viewport only (+0x2F0 non-zero: the main view, a
// reflection; not the cascades' orthographic ones). The camera: the
// viewport's camera-to-world matrix at +0x40 (rows), its translation row at
// +0x70. The layout is the Complete Edition's (gta4-rtx structs.hpp
// grcViewport), the same in 1.0.8.0: the field of view at +0x2B8 that
// ff_shaders.cpp reads, the planes at +0x300 that 004151B0 reads.

#pragma once

#include <cstdint>

namespace gtaiv_anticull {

inline constexpr float kNearAll = 20.0f;  // metres from the camera: never culled (gta4-rtx nocull_dist_lights)

// grcViewport offsets.
inline constexpr uint32_t kCameraPos = 0x70, kPerspective = 0x2F0, kPlanes = 0x300;

// Whether a sphere with centre p is kept (inside) by the near rule for a
// viewport whose camera is at `cam`, perspective or not.
inline bool KeptNear(const float cam[3], bool perspective, const float p[3]) {
  if (!perspective) return false;
  const float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
  const float d2 = dx * dx + dy * dy + dz * dz;
  return d2 < kNearAll * kNearAll;  // (NaN: not kept)
}

// Buildings and objects (an entity's verdict, 00957450, which ends in
// 004151B0 by way of 00904D10, or in 00A1DC10: four planes from a camera
// position, thiscall on {camera xyz, pad, planes...} with the centre's
// pointer and the radius, ret 8, 0 inside), as gta4-rtx's
// static_world_culling_verdict (gta4.cpp:392-471, comp_settings.hpp
// 657-704): kept within kNearStatic of the camera; within kMidStatic with
// a radius over kMidRadius; within kFarStatic with a radius over
// kFarRadius or a height over kFarHeight (gta4-rtx nocull_height_far_static:
// its box's height, the entity's virtual +0x60 and +0x64, min and max, as
// 00957450 reads them; 0 where unknown). The game's own distance and level
// of detail tests before them stay (both levels would draw).
inline constexpr float kNearStatic = 30.0f, kMidStatic = 80.0f, kMidRadius = 26.0f, kFarStatic = 500.0f,
                       kFarRadius = 50.0f, kFarHeight = 13.0f;
inline bool KeptStatic(const float cam[3], const float p[3], float radius, float height = 0.0f) {
  const float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
  const float d2 = dx * dx + dy * dy + dz * dz;
  if (!(d2 >= 0.0f)) return false;
  return d2 < kNearStatic * kNearStatic || (d2 < kMidStatic * kMidStatic && radius > kMidRadius) ||
         (d2 < kFarStatic * kFarStatic && (radius > kFarRadius || height > kFarHeight));
}

// Map sections: the scene's walk visits only the cells a render phase
// marked (gta4-rtx gta4.cpp:855-856: what lies behind the camera never
// reaches the tests above). 0095D1B0 marks the main view's with two calls
// of 00C51E30(polygon, n, phase, callback, four flags; cdecl), each on
// its own grid (grid units = world * scale + bias: the scales at
// [00D74EFC] and [00EDF77C]), the polygon its view's footprint: with five
// points, the camera's place first and the far corners after. After each
// such call the same call again with a square of kMarkAround around the
// camera, wound as the footprint is (gta4-rtx add_near/far_grid_map_
// sections), so the cells around and behind the camera are walked too.
inline constexpr float kMarkAround = 80.0f;                                // metres each way (kMidStatic)
inline constexpr uint32_t kMarkRetA = 0x0095E391, kMarkRetB = 0x0095E628;  // after 0095D1B0's two calls
inline constexpr uint32_t kMarkScaleA = 0x00D74EFC, kMarkScaleB = 0x00EDF77C;

// The square for a footprint `poly` (n points, x y pairs, grid units):
// centred on its first point, half `half` grid units, wound as the
// footprint (its signed area's sign). False where the footprint is not a
// perspective view's (n != 5) or the numbers are not finite.
inline bool MarkSquare(const float* poly, uint32_t n, float half, float out[8]) {
  if (n != 5 || !(half > 0.0f)) return false;
  float area = 0.0f;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t j = (i + 1) % n;
    area += poly[2 * i] * poly[2 * j + 1] - poly[2 * j] * poly[2 * i + 1];
  }
  const float cx = poly[0], cy = poly[1];
  if (!(area == area) || !(cx == cx) || !(cy == cy) || area == 0.0f) return false;
  const float sq[8] = {cx - half, cy - half, cx + half, cy - half, cx + half, cy + half, cx - half, cy + half};
  // (sq is counter-clockwise, area > 0 in x-right y-up terms.)
  for (int k = 0; k < 4; k++) {
    const int from = area > 0.0f ? k : 3 - k;
    out[2 * k] = sq[2 * from];
    out[2 * k + 1] = sq[2 * from + 1];
  }
  return true;
}

// Shadows: whatever casts a shadow into the view, though it lies outside
// it (above the top of the picture, behind the camera, toward the sun).
// 2026-11-11, his drive by day (the flight recorder's traced mask): an
// overhead lattice's shadow lay on the road ahead in four frames and was
// gone in the fifth as the lattice left the top of the view, more than
// kNearAll away and not big enough for the static rule. A sphere is kept
// when the capsule it sweeps along its shadow (the way the sun's light goes,
// `down`, unit, over kShadowReach metres) is inside all six planes: for each
// plane, a x + b y + c z + d + r + max(0, kShadowReach (a, b, c) . down) >= 0
// (the same test as the game's, of the swept sphere; it is conservative as
// the game's is). The sun's direction: the renderer's last traced one
// (gfx::RtSunToward); none yet: no such keeping.
inline constexpr float kShadowReach = 120.0f;  // metres: a shadow of a 40 m caster at a sun 18 degrees high
inline bool KeptShadow(const float planes[24], const float p[3], float radius, const float down[3]) {
  for (int k = 0; k < 6; k++) {
    const float* q = planes + 4 * k;
    const float along = q[0] * down[0] + q[1] * down[1] + q[2] * down[2];
    const float v = q[0] * p[0] + q[1] * p[1] + q[2] * p[2] + q[3] + radius + (along > 0.0f ? kShadowReach * along : 0.0f);
    if (!(v >= 0.0f)) return false;  // (NaN: not kept)
  }
  return true;
}

// What 004151B0 answers for a kept sphere: 1 with the out pointer (plane
// 0's value written there as the game does), 2 without.
inline uint32_t KeptAnswer(bool has_out) { return has_out ? 1u : 2u; }

// Plane 0's value for the sphere (what the game writes through the
// pointer): a x + b y + c z + d + r.
inline float PlaneZero(const float plane[4], const float p[3], float radius) {
  return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3] + radius;
}

}  // namespace gtaiv_anticull
