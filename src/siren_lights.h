// Emergency light bars as lights of their own while rays light the scene.
//
// The game draws a siren as a corona a bulb (00A6F7B0: no light at all, and
// nothing a ray can see) plus ONE point light 2 m above the mean of the lit
// bulbs, red or blue by a sine's sign (the siren functions 008F8960,
// 008F8EB0, 008F93B0, 008F9760, 008F9A50, 008FA180, called from 008FABD0,
// and 008FA670, each ending in one 00A2E5F0(0, 0, 0x110, ...) with radius
// 12). A bulb that turns hands its corona the bone's world axis it faces
// (the corona's last pointer, the bone matrix's row at +0x10): a beacon.
//
// With ray-traced lighting a turning bulb is a spot light at the bulb,
// along that axis tipped a little toward the ground, in the corona's colour
// made pure: its beam sweeps round the car over the street and the walls,
// shadowed by rays like every other light of the frame's list, and lights
// people and vehicles through the same list. One bulb a colour is lit
// (Pick): a light a bulb, seven on a police car, turned the street pink
// where red and pale blue beams overlapped, read as a pulse and not as a
// turn, and cost seven lights a car. The light above the roof is then left
// out, unless the siren also has bulbs that only flash (coronas without an
// axis: it stands for those). siren_lights.cpp has the hooks; this file
// what a bulb's light is.

#pragma once

#include <cmath>
#include <cstdint>

namespace gtaiv_siren {

inline constexpr int kMaxBulbs = 16;

// Sirens that get beams: those within kBeamReach metres of the camera, and
// of them at most kBeamCars a frame; the others keep the game's one light
// above the roof. A chase put ten police cars on screen: twenty spot lights
// of 25 m, each shadowed by a ray at every pixel of every vehicle near it
// (the forward pass went from 3 ms to 18).
inline constexpr float kBeamReach = 40.0f;
inline constexpr int kBeamCars = 2;
inline bool WithinReach(const float cam[3], const float pos[3]) {
  const float dx = pos[0] - cam[0], dy = pos[1] - cam[1], dz = pos[2] - cam[2];
  return dx * dx + dy * dy + dz * dz < kBeamReach * kBeamReach;  // (NaN: not within)
}

// What a bulb's light is made with (00A2E5F0's units: the colour 0-255 a
// channel, the cone's half angles in degrees).
struct Tuning {
  // (0.5 read as too bright in play: a red beam
  // at 0.3, and a blue one at blue_scale of that, as blue flashing lights
  // are seen as brighter and more glaring than red ones of the same
  // intensity at night: SAE 2019-01-0847, Bullough et al.)
  float intensity = 0.3f;
  float blue_scale = 0.5f;
  float radius = 25.0f;
  float inner = 12.0f, outer = 32.0f;
  float down = 0.2f;  // how far the beam tips toward the ground (the axis's z less this, before its length is 1)
};

// A corona's colour as a light's: its least channel taken out (a share
// kPure of it), the greatest back at 255. The game's pale blue corona
// (128, 128, 255) is a blue light, its red (255, 0, 0) stays.
inline constexpr float kPure = 0.9f;
inline void PureColour(uint8_t r, uint8_t g, uint8_t b, float out[3]) {
  const float c[3] = {float(r), float(g), float(b)};
  const float lo = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
  float hi = 0.0f;
  for (int k = 0; k < 3; k++) {
    out[k] = c[k] - kPure * lo;
    if (out[k] > hi) hi = out[k];
  }
  for (int k = 0; k < 3; k++) out[k] = hi > 0.0f ? out[k] * 255.0f / hi : 0.0f;
}

// A corona the siren drew: where, its colour bytes, and the axis its bulb
// faces (has_axis false: a bulb that only flashes).
struct Bulb {
  float pos[3] = {};
  float axis[3] = {};
  bool has_axis = false;
  uint8_t r = 0, g = 0, b = 0;
};

// A spot light for 00A2E5F0: unit direction, a unit tangent across it.
struct Spot {
  float dir[3], tangent[3], pos[3], colour[3];
  float intensity, radius, inner, outer;
};

// The bulb's light; false for one without a usable axis (none, not finite,
// or of no length) or without colour.
inline bool SpotOf(const Bulb& b, const Tuning& t, Spot* out) {
  if (!b.has_axis || (b.r | b.g | b.b) == 0) return false;
  const float l2 = b.axis[0] * b.axis[0] + b.axis[1] * b.axis[1] + b.axis[2] * b.axis[2];
  if (!(l2 > 1e-8f) || !std::isfinite(l2)) return false;
  for (int k = 0; k < 3; k++)
    if (!std::isfinite(b.pos[k])) return false;
  const float inv = 1.0f / std::sqrt(l2);
  Spot s;
  for (int k = 0; k < 3; k++) {
    s.dir[k] = b.axis[k] * inv;
    s.pos[k] = b.pos[k];
  }
  // Tipped toward the ground.
  s.dir[2] -= t.down;
  const float dl = 1.0f / std::sqrt(s.dir[0] * s.dir[0] + s.dir[1] * s.dir[1] + s.dir[2] * s.dir[2]);
  for (int k = 0; k < 3; k++) s.dir[k] *= dl;
  // Across the direction: the world's up, or its x where the beam points
  // up or down.
  const float up[3] = {0.0f, 0.0f, 1.0f}, side[3] = {1.0f, 0.0f, 0.0f};
  const float* a = std::fabs(s.dir[2]) < 0.9f ? up : side;
  const float d = a[0] * s.dir[0] + a[1] * s.dir[1] + a[2] * s.dir[2];
  float tl = 0.0f;
  for (int k = 0; k < 3; k++) {
    s.tangent[k] = a[k] - d * s.dir[k];
    tl += s.tangent[k] * s.tangent[k];
  }
  tl = 1.0f / std::sqrt(tl);
  for (int k = 0; k < 3; k++) s.tangent[k] *= tl;
  PureColour(b.r, b.g, b.b, s.colour);
  s.intensity = t.intensity * (b.b > b.r ? t.blue_scale : 1.0f);
  s.radius = t.radius;
  s.inner = t.inner;
  s.outer = t.outer;
  *out = s;
  return true;
}

// Which of `n` bulbs are lit, one a colour: of the first bulb's colour the
// first, of every other colour the last (the two ends of a bar whose bulbs
// alternate). lit[i] set for those.
inline void Pick(const Bulb* b, int n, bool* lit) {
  for (int i = 0; i < n; i++) lit[i] = false;
  for (int i = 0; i < n; i++) {
    if (!b[i].has_axis) continue;
    const bool first = b[i].r == b[0].r && b[i].g == b[0].g && b[i].b == b[0].b;
    int pick = i;
    for (int j = first ? i - 1 : i + 1; j >= 0 && j < n; j += first ? -1 : 1)
      if (b[j].has_axis && b[j].r == b[i].r && b[j].g == b[i].g && b[j].b == b[i].b) pick = j;
    lit[pick] = true;
  }
}

}  // namespace gtaiv_siren
