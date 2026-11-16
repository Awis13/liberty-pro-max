// Ray-traced reflections in the game's deferred lighting: the pixel shaders
// that sample the reflection paraboloid (deferred_lighting.fxc: pixel
// shaders 1 and 2 of lightShadowDirectional, 3 and 4 of lightNoDirectional,
// the shadows-off path) blend the GPU's traced reflections over that
// sample. Used by shadows_lights.cpp (the game's shader hooks) and
// tests/reflection_test.cpp.
//
// The vanilla shaders compute the environment term as (win32_30 pixel
// shader 1; the others name other registers):
//   texldl r3, r5, s5            the paraboloid (ParabSampler), the gloss's mip
//   mul r1.yzw, r0.z, r3.xxyz    times the horizon fade, sat(5 R'.z)
//   mul r0.xzw, r0.x, r1.yyzw    times the distance fade, 1 - sat(d / 100)
//   ... times the vertex colour's red, globalScalars.w, the Fresnel term,
//   10 (a def's constant), the sun's specular added, times 2 spec.
// PatchReflectionLookup adds, after the texldl (D its destination, h and f
// the two fades' scalars):
//   texld r8, v0, s3             the traced reflections at this pixel
//   lrp D.xyz, r8.w, r8, D       their colour where a ray hit (a 1)
//   max h, h, r8.w               and no horizon fade there
//   max f, f, r8.w               nor distance fade
// and dcl_2d s3 with the other declarations. Where nothing was hit (a 0)
// the shader computes what it did. The game's own weighting (Fresnel, the
// specular intensity, the vertex colour, the 10) still applies: the
// renderer writes the reflected light over 10 (RayReflection::
// output_scale). r8 and s3 are used by none of these shaders (checked).
// v0 is the coordinate the shader reads its G-buffer with.
//
// Found by what does not depend on register allocation (the nv6 and nv7
// folders and pixel shaders 3 and 4 allocate theirs differently): the one
// texldl from the sampler the CTAB names ParabSampler, followed by
// "mul X, h, D" and "mul Y, f, X" (h and f single components of temps).
// The sun shadow's patch (sun_shadow_tokens.h) replaces a block before the
// texldl in pixel shaders 1 and 2: both apply, in either order.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"
#include "sun_shadow_tokens.h"

namespace gtaiv_reflection {

// Where the traced reflections go, and the temp the patch takes.
inline constexpr uint32_t kOutputSampler = 3, kTemp = 8;

// What the lighting shaders read, for the renderer (gfx::RayReflection):
// the G-buffer's normal (GBufferTextureSampler1: xyz as unorm; the alpha's
// extra bits are not read), specular intensity (x) and gloss (y) in
// GBufferTextureSampler2, the Blinn-Phong exponent 512 gloss^2 (the
// shaders' "mul r3.w, r4.y^2, 512"), the blur of the paraboloid's
// sample 1 - sat(e dReflectionParams.x) (its mip is 4 times that), the
// sun's diffuse sat((N.L - 0.25) * 1.3333), the ambient gLightAmbient0 +
// gLightAmbient1 sat(0.5 - 0.5 N.z) (Z up), and the wet roads of the
// shaders with a stencil: w = sat(4 N.z) dReflectionParams.y where the
// stencil is 0, the exponent max(e, mix(e, min(8 e, 256), w)).
inline constexpr float kExponentScale = 512.0f, kDiffuseWrap = 0.25f;
inline constexpr float kTermScale = 10.0f;  // the shaders' "mul ..., c6.x (10)" on the environment term
inline constexpr float kWetSlope = 4.0f, kWetGain = 8.0f, kWetMaxExponent = 256.0f;
// A ray's length at the smoothest gloss, metres (FidelityFX's 100: the
// game's own reflection fades out by 100 m too).
inline constexpr float kMaxDistance = 100.0f;

// Where the lighting shaders keep what the renderer traces with, by the
// names in their CTAB; the stencil -1 where a shader has none.
struct Ctab {
  int parab = -1, depth = -1, normal = -1, surface = -1, stencil = -1;
  int light = -1, colour = -1, ambient0 = -1, ambient1 = -1, params = -1;
};
// False unless every one but the stencil is named: the patch and the
// consumer take the same shaders (a patched one the consumer did not take
// would read black at s3, its fades dropped).
inline bool ReadCtab(const std::vector<uint32_t>& t, Ctab* c) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  c->parab = CtabRegister(t, "ParabSampler", kSamplerSet);
  c->depth = CtabRegister(t, "GBufferTextureSampler3", kSamplerSet);
  c->normal = CtabRegister(t, "GBufferTextureSampler1", kSamplerSet);
  c->surface = CtabRegister(t, "GBufferTextureSampler2", kSamplerSet);
  c->stencil = CtabRegister(t, "GBufferStencilTextureSampler", kSamplerSet);
  c->light = CtabRegister(t, "gDirectionalLight", kFloatSet);
  c->colour = CtabRegister(t, "gDirectionalColour", kFloatSet);
  c->ambient0 = CtabRegister(t, "gLightAmbient0", kFloatSet);
  c->ambient1 = CtabRegister(t, "gLightAmbient1", kFloatSet);
  c->params = CtabRegister(t, "dReflectionParams", kFloatSet);
  return c->parab >= 0 && c->depth >= 0 && c->normal >= 0 && c->surface >= 0 && c->light >= 0 && c->colour >= 0 &&
         c->ambient0 >= 0 && c->ambient1 >= 0 && c->params >= 0;
}

namespace detail {

using gtaiv_sun_shadow::detail::Instructions;
using gtaiv_sun_shadow::detail::RegType;

constexpr uint32_t kTexld = 0x42, kTexldl = 0x5F, kDcl = 0x1F, kDef = 0x51, kDefi = 0x30, kDefb = 0x2F;

inline uint32_t Op(uint32_t t) { return t & 0xFFFF; }
inline uint32_t Len(uint32_t t) { return (t >> 24) & 0xF; }
inline bool IsTemp(uint32_t r) { return RegType(r) == 0; }
inline uint32_t Num(uint32_t r) { return r & 0x7FF; }
inline uint32_t Swizzle(uint32_t r) { return (r >> 16) & 0xFF; }
inline uint32_t Modifier(uint32_t r) { return (r >> 24) & 0xF; }

// Whether parameter token r names sampler s.
inline bool IsSampler(uint32_t r, uint32_t s) { return RegType(r) == 10 && Num(r) == s; }

// A source that is one component of a temp, unmodified (r0.z: zzzz): its
// component (-1 if not).
inline int Scalar(uint32_t r) {
  if (!IsTemp(r) || Modifier(r) != 0) return -1;
  const uint32_t s = Swizzle(r), c = s & 3;
  return s == c * 0x55 ? int(c) : -1;
}

// Whether any instruction names temp n or sampler s (the declarations
// included; a def's values are not registers).
inline bool Uses(const std::vector<uint32_t>& t, const std::vector<size_t>& at, uint32_t temp, uint32_t sampler) {
  for (size_t i : at) {
    if (Op(t[i]) == kDef || Op(t[i]) == kDefi || Op(t[i]) == kDefb) continue;
    const size_t n = Len(t[i]);
    for (size_t k = 1; k <= n && i + k < t.size(); k++) {
      if (Op(t[i]) == kDcl && k == 1) continue;  // the usage token
      const uint32_t r = t[i + k];
      if ((IsTemp(r) && Num(r) == temp) || IsSampler(r, sampler)) return true;
    }
  }
  return false;
}

// The anchor: the texldl from the paraboloid and its two muls.
struct Anchor {
  size_t texldl = 0;   // its first token
  size_t after = 0;    // the first token after it
  uint32_t dest = 0;   // D (a temp)
  uint32_t h = 0, hc = 0, f = 0, fc = 0;  // the fades' temps and components
  uint32_t coord = 0;  // the G-buffer reads' coordinate (a source token)
  size_t last_dcl = 0; // the first token after the last declaration
};

// The anchor in `t` (a ps_3_0 whose CTAB names what ReadCtab wants), and
// r8 and s3 free; false if not exactly so.
inline bool Find(const std::vector<uint32_t>& t, Anchor* a) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab ctab;
  if (!ReadCtab(t, &ctab)) return false;
  const int parab = ctab.parab, depth = ctab.depth;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  if (Uses(t, at, kTemp, kOutputSampler)) return false;
  size_t found = 0, coords = 0, k_found = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    const uint32_t op = Op(t[i]);
    if (op == kDcl) a->last_dcl = i + 1 + Len(t[i]);
    if ((op == kTexld || op == kTexldl) && Len(t[i]) == 3 && IsSampler(t[i + 3], uint32_t(parab))) {
      if (op != kTexldl) return false;
      found++;
      k_found = k;
    }
    // texld rN, vK, s<depth>: the coordinate (an input register).
    if (op == kTexld && Len(t[i]) == 3 && IsSampler(t[i + 3], uint32_t(depth)) && RegType(t[i + 2]) == 1) {
      if (coords && t[i + 2] != a->coord) return false;
      a->coord = t[i + 2];
      coords++;
    }
  }
  if (found != 1 || !coords || !a->last_dcl || k_found + 2 >= at.size()) return false;
  const size_t i = at[k_found], m1 = at[k_found + 1], m2 = at[k_found + 2];
  const uint32_t d = t[i + 1];
  if (!IsTemp(d) || ((d >> 16) & 0xF) != 0xF || ((d >> 20) & 0xFF) != 0) return false;
  // mul X, h, D.
  if (t[m1] != 0x03000005u || !IsTemp(t[m1 + 1]) || Scalar(t[m1 + 2]) < 0 || !IsTemp(t[m1 + 3]) ||
      Num(t[m1 + 3]) != Num(d) || Modifier(t[m1 + 3]) != 0)
    return false;
  // mul Y, f, X.
  if (t[m2] != 0x03000005u || !IsTemp(t[m2 + 1]) || Scalar(t[m2 + 2]) < 0 || !IsTemp(t[m2 + 3]) ||
      Num(t[m2 + 3]) != Num(t[m1 + 1]) || Modifier(t[m2 + 3]) != 0)
    return false;
  // (The first mul must not write f: the max put before it would be lost.)
  const uint32_t x = t[m1 + 1];
  if (Num(x) == Num(t[m2 + 2]) && ((x >> 16) & (1u << uint32_t(Scalar(t[m2 + 2])))) != 0) return false;
  a->texldl = i;
  a->after = i + 4;
  a->dest = Num(d);
  a->h = Num(t[m1 + 2]);
  a->hc = uint32_t(Scalar(t[m1 + 2]));
  a->f = Num(t[m2 + 2]);
  a->fc = uint32_t(Scalar(t[m2 + 2]));
  return a->last_dcl <= a->texldl;
}

inline uint32_t Temp(uint32_t n, uint32_t swizzle) { return 0x80000000u | swizzle << 16 | n; }
inline uint32_t TempDst(uint32_t n, uint32_t mask) { return 0x80000000u | mask << 16 | n; }

}  // namespace detail

// The reflections' blend after the paraboloid's sample (above). False, the
// tokens untouched, unless the shader is one of those: the CTAB's
// ParabSampler read once, by a texldl followed by the two fades' muls, r8
// and s3 unused.
inline bool PatchReflectionLookup(std::vector<uint32_t>& t) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a)) return false;
  const uint32_t w = Temp(kTemp, 0xFF);  // r8.w
  const uint32_t blend[] = {
      0x03000042u, TempDst(kTemp, 0xF), a.coord, 0xA0E40800u | kOutputSampler,    // texld r8, v0, s3
      0x04000012u, TempDst(a.dest, 0x7), w, Temp(kTemp, 0xE4), Temp(a.dest, 0xE4),  // lrp D.xyz, r8.w, r8, D
      0x0300000Bu, TempDst(a.h, 1u << a.hc), Temp(a.h, a.hc * 0x55), w,             // max h, h, r8.w
      0x0300000Bu, TempDst(a.f, 1u << a.fc), Temp(a.f, a.fc * 0x55), w,             // max f, f, r8.w
  };
  t.insert(t.begin() + std::ptrdiff_t(a.after), std::begin(blend), std::end(blend));
  const uint32_t dcl[] = {0x0200001Fu, 0x90000000u, 0xA00F0800u | kOutputSampler};  // dcl_2d s3
  t.insert(t.begin() + std::ptrdiff_t(a.last_dcl), std::begin(dcl), std::end(dcl));
  return true;
}

// Whether `t` is a shader PatchReflectionLookup made: the CTAB's
// ParabSampler read by one texldl, followed at once by the one read of s3
// (into r8), s3 declared 2D.
inline bool IsPatchedReflectionShader(const std::vector<uint32_t>& t) {
  using namespace detail;
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab ctab;
  if (!ReadCtab(t, &ctab)) return false;
  const int parab = ctab.parab;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t reads = 0, after = 0, dcls = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    if (Op(t[i]) == kDcl && Len(t[i]) == 2 && t[i + 1] == 0x90000000u && IsSampler(t[i + 2], kOutputSampler)) dcls++;
    if ((Op(t[i]) == kTexld || Op(t[i]) == kTexldl) && Len(t[i]) == 3 && IsSampler(t[i + 3], kOutputSampler)) {
      reads++;
      if (Op(t[i]) == kTexld && Num(t[i + 1]) == kTemp && IsTemp(t[i + 1]) && k > 0 && Op(t[at[k - 1]]) == kTexldl &&
          IsSampler(t[at[k - 1] + 3], uint32_t(parab)))
        after++;
    }
  }
  return reads == 1 && after == 1 && dcls == 1;
}

// The consumer's answer (GameConfig::ray_reflection_consumer) for a
// shader PatchReflectionLookup made: where the renderer binds the traced
// reflections (s3) and what it traces them from, by the CTAB's names
// (ReadCtab): the G-buffer's depth (GBufferTextureSampler3), normal (1) and
// specular intensity and gloss (2), the sun (gDirectionalLight, pointing
// away from it; gDirectionalColour), the ambient (gLightAmbient0, 1; Z up),
// dReflectionParams (x the paraboloid's sharpness, y the wetness) and,
// where the shader has one, the stencil the wet roads are told by
// (GBufferStencilTextureSampler). False for any other shader.
inline bool ReflectionConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayReflection* out) {
  Ctab c;
  if (!IsPatchedReflectionShader(t) || !ReadCtab(t, &c)) return false;
  recomp::gfx::RayReflection r;
  r.output_sampler = kOutputSampler;
  r.depth_sampler = uint32_t(c.depth);
  r.normal_sampler = uint32_t(c.normal);
  r.surface_sampler = uint32_t(c.surface);
  r.spec_channel = 0;
  r.gloss_channel = 1;
  r.exponent_scale = kExponentScale;
  r.blur_register = uint32_t(c.params);
  r.blur_component = 0;
  r.light_register = uint32_t(c.light);
  r.toward_light = false;
  r.colour_register = uint32_t(c.colour);
  r.ambient_registers[0] = uint32_t(c.ambient0);
  r.ambient_registers[1] = uint32_t(c.ambient1);
  r.up[0] = 0, r.up[1] = 0, r.up[2] = 1;
  r.diffuse_wrap = kDiffuseWrap;
  r.output_scale = 1.0f / kTermScale;
  r.max_distance = kMaxDistance;
  if (c.stencil >= 0) {
    r.wet_slope = kWetSlope;
    r.wet_gain = kWetGain;
    r.wet_max_exponent = kWetMaxExponent;
    r.wet_register = uint32_t(c.params);
    r.wet_component = 1;
    r.stencil_sampler = uint32_t(c.stencil);
  }
  *out = r;
  return true;
}

}  // namespace gtaiv_reflection
