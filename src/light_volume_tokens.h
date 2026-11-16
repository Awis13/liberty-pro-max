// The local lights the renderer lights itself with ray-traced shadows
// (x86recomp: gfx::RayLightVolume, gfx::RayManyLights, gfx/rt_many_lights.h;
// GTAIV_RT_MANY_LIGHTS): which of the game's pixel shaders name the
// G-buffer and which draw the light volumes, by the names in their CTAB,
// and how the volumes' shaders light a pixel. Used by shadows_lights.cpp
// (GtaivRayLightVolumeConsumer), main.cpp and tests/light_volume_test.cpp.
//
// The shaders: deferred_lighting.fxc's sun pass (pixel shaders 1-4, naming
// gDirectionalLight) and light volumes (6-12 and 14-17, naming
// gDeferredLightPosition) read the G-buffer: GBufferTextureSampler0 the
// diffuse colour, 1 the normal, 2 the surface (x the specular intensity,
// half the game's; y the gloss; z a factor), 3 the depth, and
// GBufferStencilTextureSampler the pixel's class (its red times 255).
// Those naming 0, 1 and 3 are taken; the volumes are those naming
// gDeferredLightPosition too.
//
// The models, from the stock win32_30 shaders (disassembled by hand; the
// same maths in nv8 and atidx10), with L the unit vector from the point to
// the light, N the normal, I from the camera to the point:
// - Model 0, the light volumes (lightVolumePoint: ps6, and its interior,
//   exterior and class-one variants ps7, ps8, ps9; the shadowed ps14 and
//   the textured ps16 and ps17 the same but for their shadow and texture):
//     a = sat(1 - d^2 c73.x); f = a^4 - 0.1 (nothing below 0); l = c76.rgb
//     c76.w f 1.1111 cone;
//     out = l sat(dot(L, N)) albedo + 2 s.x l sat(dot(L, reflect(I,
//     N)))^(512 s.y^2), times mad_sat(s.z, s.z, 0.5) in ps6 and ps8 (every
//     pixel, exterior) and not in ps7 and ps9 (interior, class one); the
//     normal with the w channel's bits (mul r2.xyz, r1.w, c1 (0.99609375,
//     7.96875, 63.75); frc; ... mad r1.xyz, r1, 256, r3; add -127.99999).
// - Model 1, the fillers (fillerVolumePoint: ps10, interior ps11, exterior
//   ps12; the shadowed ps15):
//     a = sat(1 - d^2 c73.x); f = a^2 - 1/3 (nothing below 0); l = c76.rgb
//     c76.w f 1.5 cone; out = l sat((dot(N, L) - 0.2) 1.25) albedo; no
//     specular, no factor; the normal normalize(2 t - 0.99999).
// The classes, from the same shaders' ends: v = the class texel's red
// times 255, m = v - 128 where v >= 128 (cmp), else v;
//   interior (ps7, ps11): m - 7.9 >= 0; exterior (ps8, ps12): m - 7.9 < 0;
//   class one (ps9): m - 8 where m >= 8, then |that - 1| <= 0.1.
// Which technique draws a light (00A2C680, by its flags: rt_lights.h,
// ModelAndClass) gives its model and class.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "light_shadow_tokens.h"
#include "runtime/gfx/device.h"
#include "shader_ctab.h"

namespace gtaiv_light_volume {

// RtLight's model and class numbers (gfx::RayManyLights::models, classes).
inline constexpr uint32_t kModelLight = 0, kModelFiller = 1;
inline constexpr uint32_t kClassAll = 0, kClassInterior = 1, kClassExterior = 2, kClassOne = 3;

// The consumer's answer (GameConfig::ray_light_volume_consumer).
inline bool LightVolumeConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayLightVolume* out) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  if (t.empty() || (t[0] & 0xFFFF0000u) != 0xFFFF0000u) return false;  // pixel shaders only
  const int albedo = CtabRegister(t, "GBufferTextureSampler0", kSamplerSet);
  const int normal = CtabRegister(t, "GBufferTextureSampler1", kSamplerSet);
  const int surface = CtabRegister(t, "GBufferTextureSampler2", kSamplerSet);
  const int depth = CtabRegister(t, "GBufferTextureSampler3", kSamplerSet);
  const int cls = CtabRegister(t, "GBufferStencilTextureSampler", kSamplerSet);
  if (albedo < 0 || normal < 0 || depth < 0) return false;
  auto slot = [](int s) { return s >= 0 && s < 16 ? uint32_t(s) : 16u; };
  recomp::gfx::RayLightVolume v;
  v.volume = CtabRegister(t, "gDeferredLightPosition", kFloatSet) >= 0;
  v.albedo_sampler = slot(albedo);
  v.normal_sampler = slot(normal);
  v.surface_sampler = slot(surface);
  v.depth_sampler = slot(depth);
  v.class_sampler = slot(cls);
  *out = v;
  return true;
}

// Whether class value v (0-255) is in class k (1-3) as the shaders test it.
inline bool InClass(uint32_t k, uint32_t v) {
  const float m = float(v) - (v >= 128 ? 128.0f : 0.0f);
  switch (k) {
    case kClassInterior:
      return m - 7.9f >= 0.0f;
    case kClassExterior:
      return m - 7.9f < 0.0f;
    case kClassOne: {
      const float n = m - 7.9f >= 0.0f ? m - 8.0f : m;
      return 0.1f - std::fabs(n - 1.0f) >= 0.0f;
    }
    default:
      return true;
  }
}

// How the light volumes light a pixel (above), for GameConfig::
// ray_many_lights.
inline recomp::gfx::RayManyLights ManyLights() {
  recomp::gfx::RayManyLights c;
  recomp::gfx::RayLightModel& light = c.models[kModelLight];
  light.falloff_power = 4.0f;
  light.falloff_bias = 0.1f;
  light.falloff_scale = 1.1111112f;
  light.specular = true;
  light.spec_scale = 2.0f;
  light.exponent_scale = 512.0f;
  light.factor_classes = (1u << kClassAll) | (1u << kClassExterior);
  light.normal_low_bits = true;
  recomp::gfx::RayLightModel& filler = c.models[kModelFiller];
  filler.falloff_power = 2.0f;
  filler.falloff_bias = 1.0f / 3.0f;
  filler.falloff_scale = 1.5f;
  filler.wrap = 0.2f;
  filler.wrap_scale = 1.25f;
  filler.specular = false;
  filler.normal_low_bits = false;
  for (uint32_t k = 1; k <= 3; k++)
    for (uint32_t v = 0; v < 256; v++)
      if (InClass(k, v)) c.classes[k - 1][v >> 5] |= 1u << (v & 31);
  c.lamp_gap = gtaiv_light_shadow::kLampGap;
  // In place of the game's light volumes, every light traced (2026-11-11);
  // GTAIV_RT_MANY_LIGHTS=off|add overrides it.
  c.mode = recomp::gfx::RayManyLights::Mode::Only;
  return c;
}

}  // namespace gtaiv_light_volume
