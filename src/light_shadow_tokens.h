// Ray-traced shadows of the game's shadowed local lights (street lights,
// headlights and the others it gives a shadow map): their light volumes'
// pixel shaders (deferred_lighting.fxc's pixel shaders 14, 15 and 17:
// techniques lightVolumeShadowPoint, fillerVolumeShadowPoint and
// lightVolumeShadowTexPoint, their vs3 and vs6 passes) read the GPU's
// traced mask of the light they draw instead of the game's local shadow
// atlas (gShadowZSamplerCache, s14). Used by shadows_lights.cpp (the
// game's shader hooks) and tests/light_shadow_test.cpp.
//
// The game draws these only with its Night Shadows setting on (SETTINGS.CFG,
// [010FC318] at 1-3; Off: no light gets a shadow slot, 008C3370 gives -1)
// and with the shader folders win32_30, nv8 or atidx10 (low_ati, nv6 and
// nv7 turn the setting off). nv6's and nv7's unpack the depth from a
// colour's channels; the patch leaves them alone (the renderer traces
// from a depth value).
//
// Each of these shaders samples s14 four times (rotated taps around the
// point's place in the atlas), then, in every shader folder:
//   mad X, Y.<c>, c<dShadowParam0123>.w, Z    the depths compared
//   cmp X, X, c<1>, c<0>                      1 lit, 0 in shadow, per tap
//   dp4 D, X, c<0.25>                         D: the lit fraction
// and D is the only thing read of it after (registers differ by shader and
// folder). PatchLightShadowLookup replaces that dp4 with
//   add T.xy, c<K>.<k>, vPos.xyxy             the shader's own screen UV:
//   mul T.xy, T, c<gooDeferredLightScreenSize>.zwzw
//   texld T, T, s14                           the mask at this pixel
//   mov D, T.x
// the add and mul made from the shader's own pair (it reads the G-buffer
// at that UV; one pair a shader, "add A, c<K>.k, vPos" followed by "mul A,
// A, c<screen>.zw") into T, the temp after the highest the shader uses (so
// no liveness question). The mask has the depth's size, so the UV falls on
// the texel of the depth the pixel's point was made from; the renderer
// binds it nearest. The four taps stay (their results unread). s14 is
// already declared 2D. Lights the game gives no shadow draw with other
// shaders (lightVolumePoint) and keep no shadow.
//
// LightShadowConsumer names, by the CTAB, what the renderer traces with
// (gfx::RayShadow, local): the light's position (gDeferredLightPosition),
// 1 / radius^2 (gDeferredLightInvSqrRadius.x: the shaders' attenuation
// sat(1 - d^2 c.x)), its direction (gDeferredLightDirection: the cone is
// sat((dot(normalize(L), -dir) - gDeferredLightConeOffset.x) *
// gDeferredLightConeScale.x), L from the point to the light) and the
// depth (GBufferTextureSampler3); the lamp's gap kLampGap.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"
#include "sun_shadow_tokens.h"

namespace gtaiv_light_shadow {

// The atlas's sampler, where the mask goes.
inline constexpr uint32_t kMaskSampler = 14;
inline constexpr uint32_t kTaps = 4;
// Metres a ray ends short of the light: the light sits inside its lamp
// (a street lamp's head, a car's headlight), whose mesh would block it.
inline constexpr float kLampGap = 0.5f;

// Where the light-volume shaders keep what the patch and the renderer read,
// by the names in their CTAB.
struct Ctab {
  int atlas = -1, depth = -1;                 // samplers
  int param = -1, screen = -1;                // dShadowParam0123, gooDeferredLightScreenSize
  int position = -1, inv_sq_radius = -1, direction = -1, cone_offset = -1, cone_scale = -1;
};

inline bool ReadCtab(const std::vector<uint32_t>& t, Ctab* c) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  c->atlas = CtabRegister(t, "gShadowZSamplerCache", kSamplerSet);
  c->depth = CtabRegister(t, "GBufferTextureSampler3", kSamplerSet);
  c->param = CtabRegister(t, "dShadowParam0123", kFloatSet);
  c->screen = CtabRegister(t, "gooDeferredLightScreenSize", kFloatSet);
  c->position = CtabRegister(t, "gDeferredLightPosition", kFloatSet);
  c->inv_sq_radius = CtabRegister(t, "gDeferredLightInvSqrRadius", kFloatSet);
  c->direction = CtabRegister(t, "gDeferredLightDirection", kFloatSet);
  c->cone_offset = CtabRegister(t, "gDeferredLightConeOffset", kFloatSet);
  c->cone_scale = CtabRegister(t, "gDeferredLightConeScale", kFloatSet);
  auto in = [](int r, int n) { return r >= 0 && r < n; };
  return c->atlas == int(kMaskSampler) && in(c->depth, 16) && in(c->param, 224) && in(c->screen, 224) &&
         in(c->position, 224) && in(c->inv_sq_radius, 224) && in(c->direction, 224) && in(c->cone_offset, 224) &&
         in(c->cone_scale, 224);
}

namespace detail {

using gtaiv_sun_shadow::detail::Instructions;
using gtaiv_sun_shadow::detail::RegType;

constexpr uint32_t kTexld = 0x42, kTexldl = 0x5F, kTexldd = 0x5D, kDcl = 0x1F, kDef = 0x51, kDefi = 0x30,
                   kDefb = 0x2F, kMad = 0x04;
constexpr uint32_t kTempType = 0, kConstType = 2, kMiscType = 17, kSamplerType = 10;

inline uint32_t Op(uint32_t t) { return t & 0xFFFF; }
inline uint32_t Len(uint32_t t) { return (t >> 24) & 0xF; }
inline uint32_t Num(uint32_t r) { return r & 0x7FF; }
inline uint32_t Swizzle(uint32_t r) { return (r >> 16) & 0xFF; }
inline uint32_t SrcModifier(uint32_t r) { return (r >> 24) & 0xF; }
inline uint32_t Mask(uint32_t r) { return (r >> 16) & 0xF; }
inline uint32_t DstModifier(uint32_t r) { return (r >> 20) & 0xF; }
inline bool Relative(uint32_t r) { return (r & 0x2000) != 0; }
inline bool Is(uint32_t r, uint32_t type) { return RegType(r) == type && !Relative(r); }
inline bool IsSampler(uint32_t r, uint32_t s) { return RegType(r) == kSamplerType && Num(r) == s; }
inline bool IsRead(uint32_t op) { return op == kTexld || op == kTexldl || op == kTexldd; }
// The swizzle's component at position `pos` (0-3).
inline uint32_t Comp(uint32_t swizzle, uint32_t pos) { return (swizzle >> (2 * pos)) & 3; }
// A source read unmodified, one component replicated: that component (-1:
// not so).
inline int Replicated(uint32_t r) {
  if (SrcModifier(r) != 0) return -1;
  const uint32_t s = Swizzle(r), c = s & 3;
  return s == c * 0x55 ? int(c) : -1;
}
// A destination written plainly (no saturate, no shift).
inline bool PlainDst(uint32_t r) { return DstModifier(r) == 0 && ((r >> 24) & 0xF) == 0; }
// The positions a two-component write mask covers, in order (false: not
// two components).
inline bool Pair(uint32_t mask, uint32_t* a, uint32_t* b) {
  uint32_t n = 0, p[4];
  for (uint32_t k = 0; k < 4; k++)
    if (mask & (1u << k)) p[n++] = k;
  if (n != 2) return false;
  *a = p[0], *b = p[1];
  return true;
}

// The value of a def'd constant source (c<n>, one component replicated),
// or NaN (not def'd, not one component).
inline float DefValue(const std::vector<uint32_t>& t, const std::vector<size_t>& at, uint32_t src) {
  const int c = Replicated(src);
  if (!Is(src, kConstType) || c < 0) return std::nanf("");
  for (size_t i : at)
    if (Op(t[i]) == kDef && Len(t[i]) == 5 && Is(t[i + 1], kConstType) && Num(t[i + 1]) == Num(src)) {
      float v;
      std::memcpy(&v, &t[i + 2 + size_t(c)], 4);
      return v;
    }
  return std::nanf("");
}

// Whether the depth sampler's one read is used as a plain value: the first
// instruction after it reading its register reads only its x (the
// depth's; folders whose shaders unpack depth from a colour's channels do
// otherwise, and the renderer traces from a depth value).
inline bool DepthReadAsX(const std::vector<uint32_t>& t, const std::vector<size_t>& at, uint32_t depth) {
  size_t reads = 0, k_read = 0;
  for (size_t k = 0; k < at.size(); k++)
    if (IsRead(Op(t[at[k]])) && Len(t[at[k]]) >= 3 && IsSampler(t[at[k] + 3], depth)) reads++, k_read = k;
  if (reads != 1) return false;
  const uint32_t d = t[at[k_read] + 1];
  if (!Is(d, kTempType)) return false;
  for (size_t k = k_read + 1; k < at.size(); k++) {
    const size_t i = at[k];
    if (Op(t[i]) == kDcl || Op(t[i]) == kDef) continue;
    for (size_t n = 2; n <= Len(t[i]) && i + n < t.size(); n++)
      if (Is(t[i + n], kTempType) && Num(t[i + n]) == Num(d)) return Replicated(t[i + n]) == 0;
  }
  return false;
}

inline uint32_t TempDst(uint32_t n, uint32_t mask) { return 0x80000000u | mask << 16 | n; }
inline uint32_t Temp(uint32_t n, uint32_t swizzle) { return 0x80000000u | swizzle << 16 | n; }
inline uint32_t WithSwizzle(uint32_t r, uint32_t swizzle) { return (r & ~0x00FF0000u) | swizzle << 16; }

// What the patch puts in and where.
struct Anchor {
  size_t dp4 = 0;        // the dp4's first token
  uint32_t dest = 0;     // its destination token (D)
  uint32_t k = 0;        // the add's constant source token (c<K>, replicated)
  uint32_t vpos = 0;     // the add's vPos source token
  bool k_first = false;  // the add's constant before vPos
  uint32_t screen = 0;   // the screen size register
  uint32_t sampler = 0;  // s14's source token as the taps read it
  uint32_t temp = 0;     // T
  Ctab ctab;
};

// The shader's screen UV pair (add at instruction k, mul at k + 1): its
// tokens for Anchor; false if not one.
inline bool UvPair(const std::vector<uint32_t>& t, size_t add, size_t mul, uint32_t screen, Anchor* a) {
  if (t[add] != 0x03000002u || t[mul] != 0x03000005u) return false;
  const uint32_t d = t[add + 1];
  uint32_t p0, p1;
  if (!Is(d, kTempType) || !PlainDst(d) || !Pair(Mask(d), &p0, &p1)) return false;
  // One source vPos (its x and y at the mask's places), the other a
  // replicated constant.
  const uint32_t s0 = t[add + 2], s1 = t[add + 3];
  const bool v0 = Is(s0, kMiscType) && Num(s0) == 0, v1 = Is(s1, kMiscType) && Num(s1) == 0;
  if (v0 == v1) return false;
  const uint32_t v = v0 ? s0 : s1, c = v0 ? s1 : s0;
  if (SrcModifier(v) != 0 || Comp(Swizzle(v), p0) != 0 || Comp(Swizzle(v), p1) != 1) return false;
  if (!Is(c, kConstType) || SrcModifier(c) != 0 || Comp(Swizzle(c), p0) != Comp(Swizzle(c), p1)) return false;
  // mul A, A, c<screen> (its z and w at the mask's places).
  const uint32_t md = t[mul + 1], m0 = t[mul + 2], m1 = t[mul + 3];
  if (md != d || !Is(m0, kTempType) || Num(m0) != Num(d) || SrcModifier(m0) != 0 || Comp(Swizzle(m0), p0) != p0 ||
      Comp(Swizzle(m0), p1) != p1)
    return false;
  if (!Is(m1, kConstType) || Num(m1) != screen || SrcModifier(m1) != 0 || Comp(Swizzle(m1), p0) != 2 ||
      Comp(Swizzle(m1), p1) != 3)
    return false;
  a->k = WithSwizzle(c, Comp(Swizzle(c), p0) * 0x55);
  a->vpos = WithSwizzle(v, 0x44);  // xyxy
  a->k_first = !v0;
  return true;
}

// The anchor in `t` (a ps_3_0 whose CTAB names what ReadCtab wants): exactly
// kTaps texld from s14 and no other read of it; one mad, cmp, dp4 as above
// after the last (the cmp's constants def'd 1 and 0, the dp4's 0.25); one
// screen UV pair; the depth read as a value (DepthReadAsX); a temp left.
// False if not exactly so.
inline bool Find(const std::vector<uint32_t>& t, Anchor* a) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab ctab;
  if (!ReadCtab(t, &ctab)) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t taps = 0, last_tap = 0, anchors = 0, k_anchor = 0, pairs = 0;
  uint32_t temps = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    const uint32_t op = Op(t[i]);
    if (op != kDef && op != kDefi && op != kDefb)
      for (size_t n = 1; n <= Len(t[i]) && i + n < t.size(); n++) {
        if (op == kDcl && n == 1) continue;  // the usage token
        const uint32_t r = t[i + n];
        if (RegType(r) == kTempType) temps = Num(r) + 1 > temps ? Num(r) + 1 : temps;
      }
    if (IsRead(op) && Len(t[i]) >= 3 && IsSampler(t[i + 3], kMaskSampler)) {
      if (op != kTexld) return false;
      taps++;
      last_tap = k;
      a->sampler = t[i + 3];
    }
    if (k + 1 < at.size() && UvPair(t, i, at[k + 1], uint32_t(ctab.screen), a)) pairs++;
    if (k + 2 < at.size() && op == kMad && Len(t[i]) == 4) {
      // mad X, Y.<c>, c<param>.w, Z; cmp X, X, c, c; dp4 D, X, c.<c>.
      const size_t c = at[k + 1], d = at[k + 2];
      const uint32_t x = t[i + 1];
      const bool mad = Is(x, kTempType) && Mask(x) == 0xF && PlainDst(x) && Is(t[i + 2], kTempType) &&
                       Replicated(t[i + 2]) >= 0 && Is(t[i + 3], kConstType) &&
                       Num(t[i + 3]) == uint32_t(ctab.param) && Replicated(t[i + 3]) == 3 &&
                       Is(t[i + 4], kTempType) && SrcModifier(t[i + 4]) == 0;
      const bool cmp = t[c] == 0x04000058u && t[c + 1] == x && Is(t[c + 2], kTempType) &&
                       Num(t[c + 2]) == Num(x) && Swizzle(t[c + 2]) == 0xE4 && SrcModifier(t[c + 2]) == 0 &&
                       DefValue(t, at, t[c + 3]) == 1.0f && DefValue(t, at, t[c + 4]) == 0.0f;
      const uint32_t dd = t[d + 1];  // (one component)
      const bool dp4 = t[d] == 0x03000009u && Is(dd, kTempType) && PlainDst(dd) && Mask(dd) != 0 &&
                       (Mask(dd) & (Mask(dd) - 1)) == 0 && Is(t[d + 2], kTempType) &&
                       Num(t[d + 2]) == Num(x) && Swizzle(t[d + 2]) == 0xE4 && SrcModifier(t[d + 2]) == 0 &&
                       DefValue(t, at, t[d + 3]) == 0.25f;
      if (mad && cmp && dp4) {
        anchors++;
        k_anchor = k + 2;
        a->dp4 = d;
        a->dest = dd;
      }
    }
  }
  if (taps != kTaps || anchors != 1 || pairs != 1 || k_anchor <= last_tap || temps + 1 > 32) return false;
  if (!DepthReadAsX(t, at, uint32_t(ctab.depth))) return false;
  a->screen = uint32_t(ctab.screen);
  a->temp = temps;
  a->ctab = ctab;
  return true;
}

}  // namespace detail

// The light volume's lit fraction from the traced mask instead of the
// atlas's taps (above). False, the tokens untouched, unless the shader is
// one of those: the CTAB as ReadCtab wants, the four taps of s14, the
// mad, cmp and dp4 after them, the screen UV pair, a temp left.
inline bool PatchLightShadowLookup(std::vector<uint32_t>& t) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a)) return false;
  const uint32_t T = a.temp;
  const uint32_t first = a.k_first ? a.k : a.vpos, second = a.k_first ? a.vpos : a.k;
  const uint32_t read[] = {
      0x03000002u, TempDst(T, 0x3), first, second,                               // add T.xy, c<K>.k, vPos.xyxy
      0x03000005u, TempDst(T, 0x3), Temp(T, 0xE4), 0xA0000000u | 0xEEu << 16 | a.screen,  // mul T.xy, T, c<s>.zwzw
      0x03000042u, TempDst(T, 0xF), Temp(T, 0xE4), a.sampler,                    // texld T, T, s14
      0x02000001u, a.dest, Temp(T, 0x00),                                        // mov D, T.x
  };
  t.erase(t.begin() + std::ptrdiff_t(a.dp4), t.begin() + std::ptrdiff_t(a.dp4 + 4));
  t.insert(t.begin() + std::ptrdiff_t(a.dp4), std::begin(read), std::end(read));
  return true;
}

namespace detail {

// IsPatchedLightShader, with the CTAB read.
inline bool Patched(const std::vector<uint32_t>& t, Ctab* c) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  Ctab& ctab = *c;
  if (!ReadCtab(t, &ctab)) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t taps = 0, reads = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    if (!IsRead(Op(t[i])) || Len(t[i]) < 3 || !IsSampler(t[i + 3], kMaskSampler)) continue;
    taps++;
    if (k == 0 || k + 1 >= at.size()) continue;
    const size_t m = at[k - 1], v = at[k + 1];
    const uint32_t T = Num(t[i + 1]);
    reads += t[i] == 0x03000042u && t[i + 1] == TempDst(T, 0xF) && t[i + 2] == Temp(T, 0xE4) &&
             t[m] == 0x03000005u && t[m + 1] == TempDst(T, 0x3) && t[m + 2] == Temp(T, 0xE4) &&
             t[m + 3] == (0xA0000000u | 0xEEu << 16 | uint32_t(ctab.screen)) && t[v] == 0x02000001u &&
             t[v + 2] == Temp(T, 0x00);
  }
  return taps == kTaps + 1 && reads == 1;
}

}  // namespace detail

// Whether `t` is a shader PatchLightShadowLookup made: the CTAB ReadCtab
// wants, five texld from s14 and the last one's "texld T, T, s14" followed
// by "mov D, T.x", after "mul T.xy, T, c<screen>.zwzw".
inline bool IsPatchedLightShader(const std::vector<uint32_t>& t) {
  Ctab c;
  return detail::Patched(t, &c);
}

// The consumer's answer (GameConfig::ray_shadow_consumer) for a shader
// PatchLightShadowLookup made: a local light's mask at s14, traced from the
// depth (GBufferTextureSampler3) with the light the draw's constants hold
// (above, by the CTAB's names). False for any other shader.
inline bool LightShadowConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayShadow* out) {
  Ctab c;
  if (!detail::Patched(t, &c)) return false;
  recomp::gfx::RayShadow s;
  s.mask_sampler = kMaskSampler;
  s.depth_sampler = uint32_t(c.depth);
  s.local = true;
  s.position_register = uint32_t(c.position);
  s.radius_register = uint32_t(c.inv_sq_radius);
  s.radius_component = 0;
  s.cone = true;
  s.direction_register = uint32_t(c.direction);
  s.cone_offset_register = uint32_t(c.cone_offset);
  s.cone_offset_component = 0;
  s.cone_scale_register = uint32_t(c.cone_scale);
  s.cone_scale_component = 0;
  s.lamp_gap = kLampGap;
  *out = s;
  return true;
}

// ---- traced by the light volume's own pixels (ray queries)
//
// Where the GPU traces rays from render pipelines (gfx::
// RayTracedForwardShadows), the light volume traces its light's shadow
// itself, per pixel, every frame: no mask, no compute pass between its
// draws. (Unless the renderer has traced the light already: it traces the
// lights of the frame before once per pixel of the depth in one compute
// pass before they draw, gfx::RayForwardShadow::local, and a volume of one
// of them reads that instead of a ray per pixel of the volume, which
// overlapping volumes at a high resolution make millions a frame; the
// consumer names the light's reach and the depth for that.) Each of these
// shaders rebuilds its pixel's world point P from the depth and the camera,
// then takes the vector to the light once:
//   add L.xyz, -P, c<gDeferredLightPosition>    (either order)
// PatchLightShadowRay keeps P in a temp of its own just before that add
//   mov W.xyz, P
// and replaces the dp4 (above) with a read of s14 at W
//   texld T, W, s14
//   mov D, T.x
// which the translation makes a ray from W to the light's position
// (gfx::RayForwardShadow, point: LightShadowRayConsumer). The four taps
// read the depth's sampler instead (their results stay unread), so that
// the one read of s14 left is the ray.

namespace detail {

// The add taking the light's vector from the world point: its index in
// `at` and the point's source token (its negation dropped). False unless
// exactly one.
inline bool FindWorldPoint(const std::vector<uint32_t>& t, const std::vector<size_t>& at, uint32_t position,
                           size_t* k_add, uint32_t* point) {
  size_t found = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    if (t[i] != 0x03000002u) continue;
    const uint32_t s0 = t[i + 2], s1 = t[i + 3];
    for (int side = 0; side < 2; side++) {
      const uint32_t c = side ? s1 : s0, p = side ? s0 : s1;
      if (!Is(c, kConstType) || Num(c) != position || SrcModifier(c) != 0) continue;
      if (!Is(p, kTempType) || SrcModifier(p) != 1) continue;
      found++;
      *k_add = k;
      *point = p & ~0x0F000000u;
    }
  }
  return found == 1;
}

// RayPatched: the shader PatchLightShadowRay made, with its CTAB read.
inline bool RayPatched(const std::vector<uint32_t>& t, Ctab* c) {
  if (t.size() < 2 || t[0] != 0xFFFF0300u) return false;
  if (!ReadCtab(t, c)) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t reads = 0, rays = 0;
  for (size_t k = 0; k < at.size(); k++) {
    const size_t i = at[k];
    if (!IsRead(Op(t[i])) || Len(t[i]) < 3 || !IsSampler(t[i + 3], kMaskSampler)) continue;
    reads++;
    if (k + 1 >= at.size()) continue;
    const uint32_t T = Num(t[i + 1]);
    const size_t v = at[k + 1];
    rays += t[i] == 0x03000042u && t[i + 1] == TempDst(T, 0xF) && Is(t[i + 2], kTempType) &&
            Swizzle(t[i + 2]) == 0xE4 && SrcModifier(t[i + 2]) == 0 && t[v] == 0x02000001u &&
            t[v + 2] == Temp(T, 0x00);
  }
  return reads == 1 && rays == 1;
}

}  // namespace detail

// The light volume's lit fraction from a ray its pixels trace toward the
// light (above). False, the tokens untouched, unless the shader is one
// PatchLightShadowLookup would take, with one add from the world point to
// the light's position before its dp4 and two temps left.
inline bool PatchLightShadowRay(std::vector<uint32_t>& t) {
  using namespace detail;
  Anchor a;
  if (!Find(t, &a) || a.temp + 2 > 32) return false;
  std::vector<size_t> at;
  if (!Instructions(t, &at)) return false;
  size_t k_add = 0;
  uint32_t point = 0;
  if (!FindWorldPoint(t, at, uint32_t(a.ctab.position), &k_add, &point)) return false;
  const size_t add = at[k_add];
  if (add >= a.dp4) return false;
  const uint32_t T = a.temp, W = a.temp + 1;
  // The taps: the depth's sampler (same token, another number).
  const uint32_t depth = (a.sampler & ~0x7FFu) | uint32_t(a.ctab.depth);
  for (size_t i : at)
    if (IsRead(Op(t[i])) && Len(t[i]) >= 3 && IsSampler(t[i + 3], kMaskSampler)) t[i + 3] = depth;
  const uint32_t read[] = {
      0x03000042u, TempDst(T, 0xF), Temp(W, 0xE4), a.sampler,  // texld T, W, s14
      0x02000001u, a.dest, Temp(T, 0x00),                      // mov D, T.x
  };
  t.erase(t.begin() + std::ptrdiff_t(a.dp4), t.begin() + std::ptrdiff_t(a.dp4 + 4));
  t.insert(t.begin() + std::ptrdiff_t(a.dp4), std::begin(read), std::end(read));
  const uint32_t keep[] = {0x02000001u, TempDst(W, 0x7), point};  // mov W.xyz, P
  t.insert(t.begin() + std::ptrdiff_t(add), std::begin(keep), std::end(keep));
  return true;
}

// Whether `t` is a shader PatchLightShadowRay made: the CTAB ReadCtab
// wants, one read of s14, "texld T, W, s14" followed by "mov D, T.x".
inline bool IsRayPatchedLightShader(const std::vector<uint32_t>& t) {
  Ctab c;
  return detail::RayPatched(t, &c);
}

// The consumer's answer (GameConfig::ray_forward_shadow_consumer) for a
// shader PatchLightShadowRay made: s14's read a ray toward the local light
// at gDeferredLightPosition, ending kLampGap short of it; and the light's
// reach (gDeferredLightInvSqrRadius.x, its cone by gDeferredLightDirection,
// gDeferredLightConeOffset.x and gDeferredLightConeScale.x) and the depth
// it lights (GBufferTextureSampler3), with which the renderer traces the
// frame's lights once per pixel instead (gfx::RayForwardShadow::local).
// False for any other shader.
inline bool LightShadowRayConsumer(const std::vector<uint32_t>& t, recomp::gfx::RayForwardShadow* out) {
  Ctab c;
  if (!detail::RayPatched(t, &c)) return false;
  recomp::gfx::RayForwardShadow s;
  s.sampler = kMaskSampler;
  s.light_register = uint32_t(c.position);
  s.toward_light = true;
  s.point = true;
  s.lamp_gap = kLampGap;
  s.local = true;
  s.depth_sampler = uint32_t(c.depth);
  s.radius_register = uint32_t(c.inv_sq_radius);
  s.radius_component = 0;
  s.cone = true;
  s.direction_register = uint32_t(c.direction);
  s.cone_offset_register = uint32_t(c.cone_offset);
  s.cone_offset_component = 0;
  s.cone_scale_register = uint32_t(c.cone_scale);
  s.cone_scale_component = 0;
  *out = s;
  return true;
}

}  // namespace gtaiv_light_shadow
