// MetalFX's denoiser on the lit scene, before the post-processing (x86recomp:
// gfx::RayDenoise, gfx/rt_denoise.h): which of the game's pixel shaders
// take part, by the names in their CTAB. Used by shadows_lights.cpp
// (GtaivRayDenoiseConsumer) and tests/denoise_test.cpp.
//
// - The lighting: deferred_lighting.fxc's sun pass (pixel shaders 1 and 2
//   of lightShadowDirectional, 3 and 4 of lightNoDirectional, the ones the
//   reflections' patch takes) draws into the lit scene (the 64-bit HDR
//   target) and reads the G-buffer: GBufferTextureSampler0 the diffuse
//   colour, 1 the normal (xyz as unorm), 2 the specular intensity (x, half
//   the game's: its specular term is 2 spec) and gloss (y, the exponent 512
//   gloss^2). Told by naming all of those and gDirectionalLight (the light
//   volumes, drawn later into the same target, name no sun). The specular
//   albedo's Fresnel is the game's own environment term's: 0.25 + 0.75
//   (1 - |I.N|)^4.
// - The post-processing: rage_postfx.fxc's pixel shaders read the lit scene
//   through HDRSampler (all but the luminance adaptation and the stencil
//   copy); the denoise runs before the first of them that reads it.

#pragma once

#include <cstdint>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"

namespace gtaiv_denoise {

inline constexpr float kExponentScale = 512.0f, kSpecScale = 2.0f;
inline constexpr float kFresnelMin = 0.25f, kFresnelPower = 4.0f;

// The consumer's answer (GameConfig::ray_denoise_consumer): the sun pass's
// G-buffer, or a post-process shader's input; false for any other shader.
inline bool DenoiseConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayDenoise* out) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  if (t.empty() || (t[0] & 0xFFFF0000u) != 0xFFFF0000u) return false;  // pixel shaders only
  recomp::gfx::RayDenoise d;
  if (const int hdr = CtabRegister(t, "HDRSampler", kSamplerSet); hdr >= 0) {
    d.post_input = true;
    d.input_sampler = uint32_t(hdr);
    *out = d;
    return true;
  }
  const int albedo = CtabRegister(t, "GBufferTextureSampler0", kSamplerSet);
  const int normal = CtabRegister(t, "GBufferTextureSampler1", kSamplerSet);
  const int surface = CtabRegister(t, "GBufferTextureSampler2", kSamplerSet);
  if (albedo < 0 || normal < 0 || surface < 0 || CtabRegister(t, "gDirectionalLight", kFloatSet) < 0) return false;
  d.albedo_sampler = uint32_t(albedo);
  d.normal_sampler = uint32_t(normal);
  d.surface_sampler = uint32_t(surface);
  d.spec_channel = 0;
  d.gloss_channel = 1;
  d.exponent_scale = kExponentScale;
  d.spec_scale = kSpecScale;
  d.fresnel_min = kFresnelMin;
  d.fresnel_power = kFresnelPower;
  *out = d;
  return true;
}

}  // namespace gtaiv_denoise
