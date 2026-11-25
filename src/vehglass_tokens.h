// Ray-traced reflections on vehicle glass: the forward pixel shaders of
// gta_vehicle_vehglass.fxc that sample the reflection paraboloid (pixel
// shaders 2, 3 and 4: draw, lightweight0_draw and lightweight4_draw, and
// their skinned techniques; 3 is also reflection_draw's) blend reflections
// they trace themselves over that sample (x86recomp's gfx::
// RayForwardReflection: the read of the sampler this patch adds becomes a
// ray query). Used by shadows_lights.cpp (the game's shader hooks) and
// tests/vehglass_test.cpp.
//
// The vanilla shaders compute the environment term as (win32_30 pixel
// shader 2; the others name other registers):
//   nrm r4.xyz, v2 + 1e-5        V, toward the eye: TEXCOORD3
//   nrm r2.xyz, v1 + 1e-5        N: TEXCOORD1
//   mad r3.yzw, N, -2 (-V.N), -V R, reflected
//   nrm r5.xyz, R + 1e-5
//   ...                          the paraboloid's coordinate from it
//   texldl r4, r4, s4            the paraboloid (EnvironmentSampler), level 0
//   mul_sat r1.x, r5.z, c3.x     the horizon fade, sat(5 R.z)
//   mul r4.xyz, r4, r1.x         times it
//   ... times the Fresnel term, 10 (a def's constant), 2 globalScalars.w,
//   reflectivePowerED, 1.8, and later over the alpha (the blend multiplies
//   it back).
// PatchVehglassLookup adds, after the texldl (D its destination, h the
// fade's scalar, T and U two temps the shader leaves unused, e the eye's
// register, gViewInverse's fourth row c15, P TEXCOORD2's input, the
// pixel's world position, v4, M TEXCOORD1's, the normal, v1):
//   add T.xyz, c<e>, -P          toward the eye
//   nrm T.xyz, T                 V
//   nrm U.xyz, M                 N
//   dp3 U.w, U, T
//   add U.w, U.w, U.w            2 N.V
//   mad T.xyz, U, U.w, -T        R = 2 (N.V) N - V
//   texld T, T.xyzz, s3          the traced reflection along R (a ray query from P)
//   lrp D.xyz, T.w, T, D         its colour where a ray hit (a 1)
// and after the mul_sat:
//   max h, h, T.w                no horizon fade there
// and dcl_2d s3 with the other declarations (39 tokens). Where nothing was
// hit (a 0) the shader computes what it did. R is made again rather than
// read from the shader's own: the non-skinned vertex shader (1) writes
// TEXCOORD3 as c15 - (world position + c3), the translation added twice
// ("add r0.xyz, r0, c3; add r1.xyz, r0, c3; add o3.xyz, -r1, c15"), so the
// game's V and R are off by the car's position there; its TEXCOORD2 (o5,
// r0) and the skinned one's (2: o5 and o3 from the same r1) are right. The
// game's own weighting (Fresnel, 10, 2 globalScalars.w, reflectivePowerED,
// 1.8) still applies to the traced light as to the paraboloid's: the
// renderer writes it over 10 (RayForwardReflection::output_scale), as the
// lighting's reflections (reflection_tokens.h), which share the paraboloid
// and its 10.
//
// Found by what does not depend on register allocation (the nv6 and nv7
// folders allocate theirs differently; their pixel shader 2 uses r14): the
// one read of the sampler the CTAB names EnvironmentSampler, a texldl,
// followed by "mul_sat h, N.z, X" and "mul D.xyz, D, h", in a shader whose
// CTAB names gViewInverse and the lighting the consumer reads and declares
// TEXCOORD1 and 2. Not in gta_vehicle_lightsemissive.fxc's pixel shaders 2-4
// (the same anchor, CTAB and inputs, lamps' lenses): their CTAB names
// emissiveMultiplier, which refuses them. T and U are the temps after the
// highest the shader uses; s3 is used by none of them (checked).

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"
#include "sun_shadow_tokens.h"

namespace gtaiv_vehglass {

// Where the traced reflection is read (a ray query, not a texture).
inline constexpr uint32_t kSampler = 3;

// What the glass shaders light with, for the renderer (gfx::
// RayForwardReflection): the sun's diffuse sat((N.L - 0.25) * 1.3333)
// ("add r3.x, r3.x, c3.w (-0.25); mul_sat r3.x, r3.x, c4.w (1.3333)"), its
// colour gDirectionalColour times its w, the ambient gLightAmbient0 +
// gLightAmbient1 sat(0.5 - 0.5 N.z) (Z up), the term's def constant 10.
inline constexpr float kDiffuseWrap = 0.25f;
inline constexpr float kTermScale = 10.0f;
// A ray's length, metres (as the lighting's reflections at their sharpest:
// glass is a mirror).
inline constexpr float kMaxDistance = 100.0f;
// The world position's input (TEXCOORD2) and the normal's (TEXCOORD1).
inline constexpr uint32_t kTexcoord = 5, kOriginIndex = 2, kNormalIndex = 1;

// Where the glass shaders keep what the renderer traces with, by the names
// in their CTAB (eye: gViewInverse's fourth row, the camera's position),
// and the world position's and normal's input registers.
struct Ctab {
  int env = -1, light = -1, colour = -1, ambient0 = -1, ambient1 = -1, eye = -1;
  int origin = -1, normal = -1;
};

namespace detail {

using gtaiv_sun_shadow::detail::Instructions;
using gtaiv_sun_shadow::detail::RegType;

constexpr uint32_t kTexld = 0x42, kTexldl = 0x5F, kTexldd = 0x5D, kDcl = 0x1F, kDef = 0x51, kDefi = 0x30,
                   kDefb = 0x2F, kMul = 0x05;

inline uint32_t Op(uint32_t t) { return t & 0xFFFF; }
inline uint32_t Len(uint32_t t) { return (t >> 24) & 0xF; }
inline bool IsTemp(uint32_t r) { return RegType(r) == 0; }
inline uint32_t Num(uint32_t r) { return r & 0x7FF; }
inline uint32_t Swizzle(uint32_t r) { return (r >> 16) & 0xFF; }
inline uint32_t SrcModifier(uint32_t r) { return (r >> 24) & 0xF; }
inline uint32_t Mask(uint32_t r) { return (r >> 16) & 0xF; }
inline uint32_t DstModifier(uint32_t r) { return (r >> 20) & 0xF; }
inline bool IsSampler(uint32_t r, uint32_t s) { return RegType(r) == 10 && Num(r) == s; }
inline bool IsRead(uint32_t op) { return op == kTexld || op == kTexldl || op == kTexldd; }

// A source that is one component of a temp, unmodified (r5.z: zzzz): its
// component (-1 if not).
inline int Scalar(uint32_t r) {
  if (!IsTemp(r) || SrcModifier(r) != 0) return -1;
  const uint32_t s = Swizzle(r), c = s & 3;
  return s == c * 0x55 ? int(c) : -1;
}
// A destination that is one component of a temp: its component (-1 if not).
inline int ScalarDst(uint32_t r) {
  if (!IsTemp(r)) return -1;
  switch (Mask(r)) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: return -1;
  }
}

inline uint32_t Temp(uint32_t n, uint32_t swizzle) { return 0x80000000u | swizzle << 16 | n; }
inline uint32_t Input(uint32_t n, uint32_t swizzle) { return 0x90000000u | swizzle << 16 | n; }
inline uint32_t Const(uint32_t n, uint32_t swizzle) { return 0xA0000000u | swizzle << 16 | n; }
constexpr uint32_t kNegate = 1u << 24;
inline uint32_t TempDst(uint32_t n, uint32_t mask) { return 0x80000000u | mask << 16 | n; }

}  // namespace detail

// The CTAB's names and the inputs; false unless all are there and the
// CTAB does not name emissiveMultiplier (lamps' lenses): the patch and the
// consumer take the same shaders (a patched one the consumer did not take
// would be translated with s3 read as a texture, which nothing binds).
inline bool ReadCtab(const std::vector<uint32_t>& t, Ctab* c) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  using namespace detail;
  c->env = CtabRegister(t, "EnvironmentSampler", kSamplerSet);
  c->light = CtabRegister(t, "gDirectionalLight", kFloatSet);
  c->colour = CtabRegister(t, "gDirectionalColour", kFloatSet);
  c->ambient0 = CtabRegister(t, "gLightAmbient0", kFloatSet);
  c->ambient1 = CtabRegister(t, "gLightAmbient1", kFloatSet);
  const int view = CtabRegister(t, "gViewInverse", kFloatSet);
  c->eye = view >= 0 ? view + 3 : -1;
  c->origin = c->normal = -1;
  if (CtabRegister(t, "emissiveMultiplier", kFloatSet) >= 0) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  for (size_t i : at) {
    // dcl_texcoord<n> vN (xyz at least).
    if (Op(t[i]) != kDcl || Len(t[i]) != 2 || RegType(t[i + 2]) != 1) continue;
    const uint32_t u = t[i + 1];
    if ((u & 0x1F) != kTexcoord || (Mask(t[i + 2]) & 7) != 7) continue;
    if (((u >> 16) & 0xF) == kOriginIndex) c->origin = int(Num(t[i + 2]));
    if (((u >> 16) & 0xF) == kNormalIndex) c->normal = int(Num(t[i + 2]));
  }
  return c->env >= 0 && c->light >= 0 && c->colour >= 0 && c->ambient0 >= 0 && c->ambient1 >= 0 && c->eye >= 0 &&
         c->eye < 224 && c->origin >= 0 && c->normal >= 0;
}

namespace detail {

// The anchor: the texldl from the environment map, its fade and the mul.
struct Anchor {
  size_t texldl = 0;    // its first token
  size_t fade = 0;      // the mul_sat's first token
  size_t after_fade = 0;
  uint32_t dest = 0;    // D (a temp)
  uint32_t dir = 0;     // N (a temp: R in its xyz)
  uint32_t h = 0, hc = 0;  // the fade's temp and component
  uint32_t temp = 0;    // T (U the next)
  size_t last_dcl = 0;  // the first token after the last declaration
  Ctab ctab;
};

// The anchor in `t` (a ps_3_0 whose CTAB names what ReadCtab wants), a temp
// free after the highest used and s3 unused; false if not exactly so.
inline bool Find(const std::vector<uint32_t>& t, Anchor* a) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab ctab;
  if (!ReadCtab(t, &ctab)) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t found = 0, k_found = 0;
  uint32_t temps = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    const uint32_t op = Op(t[i]);
    if (op == kDcl) a->last_dcl = i + 1 + Len(t[i]);
    if (op != kDef && op != kDefi && op != kDefb)
      for (size_t n = 1; n <= Len(t[i]) && i + n < t.size(); n++) {
        if (op == kDcl && n == 1) continue;  // the usage token
        const uint32_t r = t[i + n];
        if (IsTemp(r)) temps = Num(r) + 1 > temps ? Num(r) + 1 : temps;
        if (IsSampler(r, kSampler)) return false;
      }
    if (IsRead(op) && Len(t[i]) >= 3 && IsSampler(t[i + 3], uint32_t(ctab.env))) {
      if (op != kTexldl) return false;
      found++;
      k_found = k;
    }
  }
  if (found != 1 || !a->last_dcl || temps + 1 >= 32 || k_found + 2 >= at.size()) return false;
  const size_t i = at[k_found], m1 = at[k_found + 1], m2 = at[k_found + 2];
  const uint32_t d = t[i + 1];
  if (!IsTemp(d) || Mask(d) != 0xF || DstModifier(d) != 0 || ((d >> 24) & 0xF) != 0) return false;
  // mul_sat h, N.z, X.
  const int hc = ScalarDst(t[m1 + 1]);
  if (t[m1] != 0x03000005u || hc < 0 || DstModifier(t[m1 + 1]) != 1 || ((t[m1 + 1] >> 24) & 0xF) != 0 ||
      Scalar(t[m1 + 2]) != 2)
    return false;
  const uint32_t h = Num(t[m1 + 1]), n = Num(t[m1 + 2]);
  if (h == Num(d) || n == Num(d)) return false;
  // mul D.xyz, D, h.
  if (t[m2] != 0x03000005u || !IsTemp(t[m2 + 1]) || Num(t[m2 + 1]) != Num(d) || Mask(t[m2 + 1]) != 7 ||
      DstModifier(t[m2 + 1]) != 0 || !IsTemp(t[m2 + 2]) || Num(t[m2 + 2]) != Num(d) || Swizzle(t[m2 + 2]) != 0xE4 ||
      SrcModifier(t[m2 + 2]) != 0 || Scalar(t[m2 + 3]) != hc || Num(t[m2 + 3]) != h)
    return false;
  a->texldl = i;
  a->fade = m1;
  a->after_fade = m1 + 4;
  a->dest = Num(d);
  a->dir = n;
  a->h = h;
  a->hc = uint32_t(hc);
  a->temp = temps;
  a->ctab = ctab;
  return a->last_dcl <= a->texldl;
}

}  // namespace detail

// The traced reflection's blend after the paraboloid's sample (above).
// False, the tokens untouched, unless the shader is one of those: the
// CTAB's EnvironmentSampler read once, by a texldl followed by the fade's
// mul_sat and mul, the CTAB and inputs ReadCtab wants, two temps left and
// s3 unused.
inline bool PatchVehglassLookup(std::vector<uint32_t>& t) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a)) return false;
  const uint32_t T = a.temp, U = a.temp + 1;
  const uint32_t e = uint32_t(a.ctab.eye), p = uint32_t(a.ctab.origin), m = uint32_t(a.ctab.normal);
  const uint32_t w = Temp(T, 0xFF);  // T.w
  const uint32_t fade[] = {
      0x0300000Bu, TempDst(a.h, 1u << a.hc), Temp(a.h, a.hc * 0x55), w,  // max h, h, T.w
  };
  t.insert(t.begin() + std::ptrdiff_t(a.after_fade), std::begin(fade), std::end(fade));
  const uint32_t blend[] = {
      0x03000002u, TempDst(T, 0x7), Const(e, 0xE4), Input(p, 0xE4) | kNegate,       // add T.xyz, c<e>, -P
      0x02000024u, TempDst(T, 0x7), Temp(T, 0xE4),                                   // nrm T.xyz, T
      0x02000024u, TempDst(U, 0x7), Input(m, 0xE4),                                  // nrm U.xyz, M
      0x03000008u, TempDst(U, 0x8), Temp(U, 0xE4), Temp(T, 0xE4),                    // dp3 U.w, U, T
      0x03000002u, TempDst(U, 0x8), Temp(U, 0xFF), Temp(U, 0xFF),                    // add U.w, U.w, U.w
      0x04000004u, TempDst(T, 0x7), Temp(U, 0xE4), Temp(U, 0xFF), Temp(T, 0xE4) | kNegate,  // mad T.xyz, U, U.w, -T
      0x03000042u, TempDst(T, 0xF), Temp(T, 0xA4), 0xA0E40800u | kSampler,           // texld T, T.xyzz, s3
      0x04000012u, TempDst(a.dest, 0x7), w, Temp(T, 0xE4), Temp(a.dest, 0xE4),       // lrp D.xyz, T.w, T, D
  };
  t.insert(t.begin() + std::ptrdiff_t(a.fade), std::begin(blend), std::end(blend));
  const uint32_t dcl[] = {0x0200001Fu, 0x90000000u, 0xA00F0800u | kSampler};  // dcl_2d s3
  t.insert(t.begin() + std::ptrdiff_t(a.last_dcl), std::begin(dcl), std::end(dcl));
  return true;
}

// Whether `t` is a shader PatchVehglassLookup made: the CTAB's
// EnvironmentSampler read by one texldl, followed within the next seven
// instructions by the one read of s3 (a texld), s3 declared 2D.
inline bool IsPatchedVehglassShader(const std::vector<uint32_t>& t) {
  using namespace detail;
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab ctab;
  if (!ReadCtab(t, &ctab)) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t reads = 0, after = 0, dcls = 0, env = SIZE_MAX;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    if (Op(t[i]) == kDcl && Len(t[i]) == 2 && t[i + 1] == 0x90000000u && IsSampler(t[i + 2], kSampler)) dcls++;
    if (Op(t[i]) == kTexldl && Len(t[i]) == 3 && IsSampler(t[i + 3], uint32_t(ctab.env))) env = k;
    if (IsRead(Op(t[i])) && Len(t[i]) >= 3 && IsSampler(t[i + 3], kSampler)) {
      reads++;
      if (Op(t[i]) == kTexld && env != SIZE_MAX && k > env && k - env <= 7) after++;
    }
  }
  return reads == 1 && after == 1 && dcls == 1;
}

// The consumer's answer (GameConfig::ray_forward_reflection_consumer) for a
// shader PatchVehglassLookup made: s3's reads become the ray query, from
// the world position (TEXCOORD2's register), the hits lit by the sun
// (gDirectionalLight, pointing away from it; gDirectionalColour) and the
// ambient (gLightAmbient0, 1; Z up), by the CTAB's names (ReadCtab). False
// for any other shader.
inline bool VehglassConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayForwardReflection* out) {
  Ctab c;
  if (!IsPatchedVehglassShader(t) || !ReadCtab(t, &c)) return false;
  recomp::gfx::RayForwardReflection r;
  r.sampler = kSampler;
  r.origin_input = uint32_t(c.origin);
  r.light_register = uint32_t(c.light);
  r.toward_light = false;
  r.colour_register = uint32_t(c.colour);
  r.ambient_registers[0] = uint32_t(c.ambient0);
  r.ambient_registers[1] = uint32_t(c.ambient1);
  r.up[0] = 0, r.up[1] = 0, r.up[2] = 1;
  r.diffuse_wrap = kDiffuseWrap;
  r.output_scale = 1.0f / kTermScale;
  r.max_distance = kMaxDistance;
  *out = r;
  return true;
}

}  // namespace gtaiv_vehglass
