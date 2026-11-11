// What the sun's shadow cascades keep (GTAIV_SHADOW_ANTICULL, on by
// default; native.cpp's 004151B0 hooks it in).
//
// The game tests each thing for each cascade with 004151B0, the sphere
// against a viewport's frustum (thiscall: ecx the grcViewport; on the stack
// x, y, z, the radius and a float* out; answers 1 inside, plane 0's value
// to out, 2 inside without out, 0 outside; ret 0x14). For the cascades the
// viewports are orthographic, and since 1.0.7.0 the culling there is so
// tight that casters near the player drop out of the shadow maps at some
// camera angles: the player's and a lamp post's shadows, a building's,
// come and go as the camera turns (GTAIV-Issues-List #17, #114, #150,
// #161; GitHub issue #50; FusionFix leaves it). Here, for an orthographic
// viewport, a sphere whose centre lies within kNear metres of the game's
// camera, or within kNearBig of it with a radius over kBigRadius, is inside
// (the ray-traced build's rule for its scene, rt_anticull.h, with only its
// near distances: the cascades draw what they keep, a raster cost).

#pragma once

#include <cmath>
#include <cstdint>

namespace shadow_anticull {

inline constexpr uint32_t kPerspective = 0x2F0;  // grcViewport: nonzero for a perspective one
inline constexpr uint32_t kPlanes = 0x300;       // grcViewport: the frustum's planes, plane 0 first
// The game's camera: the object at [kCameraTable + 4 [kCameraTable]], its
// position at +kCameraPos (as 008C4550 reads it).
inline constexpr uint32_t kCameraTable = 0x011F82A8, kCameraPos = 0x80;
inline constexpr float kNear = 30.0f, kNearBig = 80.0f, kBigRadius = 26.0f;

// Whether a sphere at p (radius r) is kept in an orthographic viewport,
// the game's camera at cam: within kNear of it, or within kNearBig with a
// radius over kBigRadius. A NaN anywhere: not kept.
inline bool Kept(const float cam[3], const float p[3], float r) {
  const float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
  const float d2 = dx * dx + dy * dy + dz * dz;
  if (!(d2 >= 0.0f) || !(r >= 0.0f)) return false;
  return d2 < kNear * kNear || (r > kBigRadius && d2 < kNearBig * kNearBig);
}

// What the test answers for a kept sphere: 1 with an out pointer, 2 without.
inline uint32_t KeptAnswer(bool has_out) { return has_out ? 1u : 2u; }

// Plane 0's value for the sphere, as the game writes it to out: a x + b y +
// c z + d + r.
inline float PlaneZero(const float plane[4], const float p[3], float r) {
  return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3] + r;
}

}  // namespace shadow_anticull
