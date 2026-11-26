// Ray-traced ambient occlusion in the sun pass: the deferred lighting's
// pixel shaders that light the G-buffer with the sun and the flat ambient
// (deferred_lighting.fxc's pixel shaders 1-4) multiply that ambient by an
// occlusion the renderer traces from the depth (x86recomp's gfx::
// RayOcclusion, gfx/rt_occlusion.h: how much of the sky above each pixel's
// point no geometry within a few metres blocks; 1 open, 0 closed) in place
// of a plain sky light that reaches under cars, into doorways and into the
// corners of walls as brightly as onto an open street. Used by
// shadows_lights.cpp (the game's shader hooks) and tests/occlusion_test.cpp.
//
// The vanilla shaders blend their two ambient colours by the normal's z and
// scale the result by the G-buffer's own term (ps1 of win32_30; the others
// name other registers and masks):
//   mad_sat r1.y, r2.z, c4.x, c4.y         the blend by N.z
//   mov r2.xyz, c38                         gLightAmbient1
//   mad r1.yzw, r2.xxyz, r1.y, c37.xxyz     A = gLightAmbient0 + gLightAmbient1 blend
//   mul r1.yzw, r4.z, r1                    times the G-buffer's term
// PatchOcclusionLookup adds, after that mad (A its destination, mask m; T a
// temp the shader leaves unused; U the coordinate the shader reads its
// depth, GBufferTextureSampler3, with: an input, v0):
//   texld T, U, s7                          the traced occlusion at the pixel
//   mul A.m, A, T.x
// and dcl_2d s7 with the other declarations (11 tokens). Found by what does
// not depend on register allocation: the one "mov X.xyz, c<gLightAmbient1>"
// followed at once by a mad whose third source is c<gLightAmbient0>, with
// X its first, in a ps_3_0 whose CTAB names both ambients and the G-buffer's
// depth and normals, the depth read from an input; s7 used by none of them
// (checked), the patches of the sun's shadow (sun_shadow_tokens.h) and the
// reflections (reflection_tokens.h) touching neither the mad nor s7, in
// either order (the reflections' r8 is the reason this one is applied after
// them in shadows_lights.cpp: it takes the temp after the highest used).

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"
#include "sun_shadow_tokens.h"

namespace gtaiv_occlusion {

// Where the traced occlusion is read.
inline constexpr uint32_t kSampler = 7;
// The rays' length, metres (a few: the corners of walls, under cars, into
// doorways; the sun's own shadows do the rest).
inline constexpr float kRadius = 3.0f;

// Where the shaders keep what the patch and the renderer read, by the
// names in their CTAB.
struct Ctab {
  int ambient0 = -1, ambient1 = -1, depth = -1, normals = -1;
};

namespace detail {

using gtaiv_sun_shadow::detail::Instructions;
using gtaiv_sun_shadow::detail::RegType;

constexpr uint32_t kTexld = 0x42, kTexldl = 0x5F, kTexldd = 0x5D, kDcl = 0x1F, kDef = 0x51, kDefi = 0x30, kDefb = 0x2F,
                   kMov = 0x01, kMad = 0x04, kMul = 0x05;

inline uint32_t Op(uint32_t t) { return t & 0xFFFF; }
inline uint32_t Len(uint32_t t) { return (t >> 24) & 0xF; }
inline bool IsTemp(uint32_t r) { return RegType(r) == 0; }
inline uint32_t Num(uint32_t r) { return r & 0x7FF; }
inline uint32_t Mask(uint32_t r) { return (r >> 16) & 0xF; }
inline uint32_t SrcModifier(uint32_t r) { return (r >> 24) & 0xF; }
inline uint32_t DstModifier(uint32_t r) { return (r >> 20) & 0xF; }
inline bool IsSampler(uint32_t r, uint32_t s) { return RegType(r) == 10 && Num(r) == s; }
inline bool IsConst(uint32_t r, int n) { return n >= 0 && RegType(r) == 2 && Num(r) == uint32_t(n); }
inline bool IsRead(uint32_t op) { return op == kTexld || op == kTexldl || op == kTexldd; }
inline uint32_t Temp(uint32_t n, uint32_t swizzle) { return 0x80000000u | swizzle << 16 | n; }
inline uint32_t TempDst(uint32_t n, uint32_t mask) { return 0x80000000u | mask << 16 | n; }

}  // namespace detail

// The CTAB's names; false unless all four are there.
inline bool ReadCtab(const std::vector<uint32_t>& t, Ctab* c) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  c->ambient0 = CtabRegister(t, "gLightAmbient0", kFloatSet);
  c->ambient1 = CtabRegister(t, "gLightAmbient1", kFloatSet);
  c->depth = CtabRegister(t, "GBufferTextureSampler3", kSamplerSet);
  c->normals = CtabRegister(t, "GBufferTextureSampler1", kSamplerSet);
  return c->ambient0 >= 0 && c->ambient1 >= 0 && c->depth >= 0 && c->depth < 16 && c->normals >= 0 && c->normals < 16;
}

namespace detail {

struct Anchor {
  size_t after_mad = 0;  // the first token after the ambient's mad
  uint32_t dest = 0;     // A
  uint32_t mask = 0;     // its write mask
  uint32_t coord = 0;    // U (the depth read's coordinate token)
  uint32_t temp = 0;     // T
  size_t last_dcl = 0;   // the first token after the last declaration
  size_t reads = 0;      // reads of s7 (0 before the patch)
  size_t dcls = 0;       // its declarations
  size_t read_at = 0;    // the instruction index of the (one) read of s7
  size_t mad_k = 0;      // the ambient mad's instruction index
  std::vector<size_t> at;
};

// The anchor in `t`; false unless exactly one, the depth read from an input
// register and a temp left.
inline bool Find(const std::vector<uint32_t>& t, Anchor* a) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab c;
  if (!ReadCtab(t, &c)) return false;
  if (!Instructions(t, &a->at)) return false;
  const std::vector<size_t>& at = a->at;
  size_t found = 0;
  bool coord = false;
  uint32_t temps = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    const uint32_t op = Op(t[i]);
    if (op == kDcl) {
      a->last_dcl = i + 1 + Len(t[i]);
      if (Len(t[i]) == 2 && IsSampler(t[i + 2], kSampler)) a->dcls++;
    }
    if (op != kDef && op != kDefi && op != kDefb)
      for (size_t n = 1; n <= Len(t[i]) && i + n < t.size(); n++) {
        if (op == kDcl && n == 1) continue;  // the usage token
        const uint32_t r = t[i + n];
        if (IsTemp(r) && Num(r) + 1 > temps) temps = Num(r) + 1;
      }
    if (IsRead(op) && Len(t[i]) >= 3 && IsSampler(t[i + 3], kSampler)) a->reads++, a->read_at = k;
    if (op == kTexld && Len(t[i]) == 3 && IsSampler(t[i + 3], uint32_t(c.depth)) && !coord) {
      if (RegType(t[i + 2]) != 1 || SrcModifier(t[i + 2]) != 0) return false;
      a->coord = t[i + 2];
      coord = true;
    }
    // mov X.xyz, c<ambient1>; mad A.m, X.?, s, c<ambient0>.?
    if (t[i] == 0x02000001u && IsTemp(t[i + 1]) && Mask(t[i + 1]) == 7 && DstModifier(t[i + 1]) == 0 &&
        IsConst(t[i + 2], c.ambient1) && SrcModifier(t[i + 2]) == 0 && k + 1 < at.size()) {
      const size_t m = at[k + 1];
      if (t[m] == 0x04000004u && IsTemp(t[m + 1]) && DstModifier(t[m + 1]) == 0 && ((t[m + 1] >> 24) & 0xF) == 0 &&
          IsTemp(t[m + 2]) && Num(t[m + 2]) == Num(t[i + 1]) && SrcModifier(t[m + 2]) == 0 &&
          IsConst(t[m + 4], c.ambient0) && SrcModifier(t[m + 4]) == 0) {
        found++;
        a->after_mad = m + 5;
        a->dest = Num(t[m + 1]);
        a->mask = Mask(t[m + 1]);
        a->mad_k = k + 1;
      }
    }
  }
  a->temp = temps;
  return found == 1 && coord && a->last_dcl && temps + 1 < 32 && a->last_dcl <= a->after_mad;
}

}  // namespace detail

// The occlusion's read and multiply after the ambient's mad (above). False,
// the tokens untouched, unless the shader is one of those: the anchor once,
// the CTAB's names, the depth read from an input, a temp left and s7
// unused.
// `bounce`: what s7 holds is the far light bounced once and the sky
// light's occlusion (x86recomp's RtReflector::EncodeBounce: rgb the light
// arriving, a the occlusion, 1 open):
//   texld T, U, s7
//   mad A.m, A, T.w, T.<rgb in m's components>
// (m must name three components.)
inline bool PatchOcclusionLookup(std::vector<uint32_t>& t, bool bounce = false) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a) || a.reads || a.dcls) return false;
  const uint32_t T = a.temp;
  uint32_t swizzle = 0, channel = 0;
  for (uint32_t c = 0; c < 4; c++) {
    swizzle |= (channel < 3 ? channel : 2u) << (2 * c);
    if (a.mask & (1u << c)) channel++;
  }
  if (bounce && channel != 3) return false;
  std::vector<uint32_t> code = {0x03000042u, TempDst(T, 0xF), a.coord, 0xA0E40800u | kSampler};  // texld T, U, s7
  if (bounce)  // mad A.m, A, T.w, T.rgb
    code.insert(code.end(), {0x04000004u, TempDst(a.dest, a.mask), Temp(a.dest, 0xE4), Temp(T, 0xFF), Temp(T, swizzle)});
  else  // mul A.m, A, T.x
    code.insert(code.end(), {0x03000005u, TempDst(a.dest, a.mask), Temp(a.dest, 0xE4), Temp(T, 0x00)});
  t.insert(t.begin() + std::ptrdiff_t(a.after_mad), code.begin(), code.end());
  const uint32_t dcl[] = {0x0200001Fu, 0x90000000u, 0xA00F0800u | kSampler};  // dcl_2d s7
  t.insert(t.begin() + std::ptrdiff_t(a.last_dcl), std::begin(dcl), std::end(dcl));
  return true;
}

// Whether `t` is a shader PatchOcclusionLookup made: the anchor once, s7
// declared 2D once and read once, by the instruction right after the
// ambient's mad, and that read's result multiplying the ambient next.
inline bool IsPatchedOcclusionShader(const std::vector<uint32_t>& t) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a) || a.reads != 1 || a.dcls != 1 || a.read_at != a.mad_k + 1 || a.read_at + 1 >= a.at.size())
    return false;
  const size_t r = a.at[a.read_at], m = a.at[a.read_at + 1];
  return t[r] == 0x03000042u && (t[m] == 0x03000005u || t[m] == 0x04000004u) && IsTemp(t[m + 1]) &&
         Num(t[m + 1]) == a.dest &&
         IsTemp(t[m + 3]) && Num(t[m + 3]) == Num(t[r + 1]);
}

// The consumer's answer (GameConfig::ray_occlusion_consumer) for a shader
// PatchOcclusionLookup made: the occlusion at s7, traced from the depth and
// the normals the CTAB names. False for any other shader.
inline bool OcclusionConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayOcclusion* out) {
  Ctab c;
  if (!IsPatchedOcclusionShader(t) || !ReadCtab(t, &c)) return false;
  recomp::gfx::RayOcclusion o;
  o.occlusion_sampler = kSampler;
  o.depth_sampler = uint32_t(c.depth);
  o.normal_sampler = uint32_t(c.normals);
  *out = o;
  return true;
}

}  // namespace gtaiv_occlusion
