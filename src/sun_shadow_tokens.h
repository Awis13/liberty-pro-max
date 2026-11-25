// Ray-traced sun shadows in the game's deferred lighting: the sun pass's
// pixel shaders (deferred_lighting.fxc, technique lightShadowDirectional:
// its two pixel shaders) read the GPU's traced shadow mask instead of the
// cascaded shadow maps. Used by shadows_lights.cpp (the game's shader
// hooks) and tests/sun_shadow_test.cpp.
//
// The vanilla shaders compute, after reading the depth (texld r0, v0, s4)
// and making the linear depth in r0.x (rcp r0.x, r0.x):
//   mad r0.yzw, v1.xxyz, -r0.x, c15.xxyz    the world position
//   ... the cascade picked, the position in its shadow map, 12 Poisson
//   taps (texld rN, ..., s15) compared, faded out with distance ...
//   cmp_sat r0.y, r0.z, r0.y, r1.x          r0.y: 1 lit, 0 in shadow
// and after it read only r0.x and r0.y (r1-r3 and r0.zw are written again
// before any read). PatchSunShadowLookup puts in that block's place
//   texld r1, v0, s15                       the mask at this pixel
//   mov r0.y, r1.x
// v0 is the G-buffer's texture coordinate of the pixel (its half-texel
// offset included), s15 already declared 2D: the renderer binds the mask
// there (gfx::RayShadow). D3D9 bytecode has no branch offsets in such a
// block, so tokens can go.
//
// Only the game's own shaders match (their CTAB, the 12 taps, the two
// instructions); FusionFix's lighting shaders are other code and keep
// their shadows. The forward-drawn shaders (decals, glass, people,
// vehicles: 12 taps of their own) and gbufferDepthCopy's one tap (pass 6)
// trace their own (forward_shadow_tokens.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "shader_ctab.h"

namespace gtaiv_sun_shadow {

// The block's first and last instructions, and what replaces it.
inline constexpr uint32_t kStart[5] = {0x04000004, 0x800E0000, 0x90900001, 0x81000000, 0xA090000F};
inline constexpr uint32_t kEnd[5] = {0x04000058, 0x80120000, 0x80AA0000, 0x80550000, 0x80000001};
inline constexpr uint32_t kMaskRead[7] = {0x03000042, 0x800F0001, 0x90E40000, 0xA0E4080F,  // texld r1, v0, s15
                                          0x02000001, 0x80020000, 0x80000001};             // mov r0.y, r1.x
// Where the patched shaders read what (gfx::RayShadow): the mask at s15,
// the depth at s4 (GBufferTextureSampler3), the sun's direction in c17
// (gDirectionalLight; the lighting's dp3 with -c17: it points away from
// the sun).
inline constexpr uint32_t kMaskSampler = 15, kDepthSampler = 4, kLightRegister = 17;
inline constexpr uint32_t kTaps = 12;

namespace detail {

constexpr uint32_t kComment = 0xFFFE, kEndToken = 0xFFFF, kTexld = 0x42;

inline uint32_t RegType(uint32_t t) { return ((t >> 28) & 7) | ((t >> 8) & 0x18); }

// The sampler register the CTAB names `name` (-1: not named).
inline int CtabSampler(const std::vector<uint32_t>& tokens, const char* name) {
  return gtaiv_shader::CtabRegister(tokens, name, gtaiv_shader::kSamplerSet);
}

// Instruction starts (comments skipped); false if the stream is cut short.
inline bool Instructions(const std::vector<uint32_t>& tokens, std::vector<size_t>* at) {
  at->clear();
  for (size_t i = 1; i < tokens.size();) {
    const uint32_t op = tokens[i] & 0xFFFF;
    if (op == kEndToken) return true;
    const size_t n = op == kComment ? (tokens[i] >> 16) & 0x7FFF : (tokens[i] >> 24) & 0xF;
    if (i + 1 + n > tokens.size()) return false;
    if (op != kComment) at->push_back(i);
    i += 1 + n;
  }
  return false;
}

inline bool Matches(const std::vector<uint32_t>& tokens, size_t i, const uint32_t* seq, size_t n) {
  return i + n <= tokens.size() && std::memcmp(&tokens[i], seq, n * 4) == 0;
}

// texld from sampler 15 at instruction start i.
inline bool TapS15(const std::vector<uint32_t>& tokens, size_t i) {
  if ((tokens[i] & 0xFFFF) != kTexld || ((tokens[i] >> 24) & 0xF) < 3 || i + 3 >= tokens.size()) return false;
  const uint32_t s = tokens[i + 3];
  return RegType(s) == 10 && (s & 0x7FF) == 15;
}

}  // namespace detail

// The G-buffer's normals (RayShadow::normal_sampler: the shadow rays start
// along them only where the depth gives no normal; they are normal-mapped):
// the sampler the CTAB names GBufferTextureSampler1. World space: the
// lighting decodes them as 256 t.xyz - 128 plus the alpha's low bits,
// normalized (the direction of 2 t - 1 to within those bits), and dots
// them with -c17, the sun's world-space direction the mask traces; 16
// (none) where the CTAB names none.
inline uint32_t SunShadowNormalSampler(const std::vector<uint32_t>& tokens) {
  const int s = detail::CtabSampler(tokens, "GBufferTextureSampler1");
  return s >= 0 && s < 16 ? uint32_t(s) : 16u;
}

// The float register of the sun's colour (gDirectionalColour, c18 in the
// game's shaders: rgb times w its intensity; every term the shadow scales
// is a product of it), 224 if the CTAB does not name it.
// The light the renderer takes as dim (gfx::RayShadow::dim_light: its mask
// traced at a sixteenth of the pixels): the colour's brightest component
// times its multiplier below this. The timecycle's directional light
// (update/pc/data/timecyc.dat, Dir RGB and DirLightMult, which the game
// puts in gDirectionalColour: 13:00 EXTRASUNNY measured (0.91 0.74 0.50) x
// 15.2) is 6-21 by day and at dusk (CLOUDY 8PM's blue 5.8), 1-2.7 at night
// (the moon: EXTRASUNNY midnight (0.18 0.73 0.77) x 1.0, SUNNY 2.0; but
// DRIZZLE's midnight 4.0), black in RAIN and LIGHTNING nights; below 3 by
// day only in LIGHTNING's mornings and FOGGY's dawn and evenings, whose
// shadows are faint.
inline constexpr float kDimLight = 3.0f;

inline uint32_t SunShadowColourRegister(const std::vector<uint32_t>& tokens) {
  const int c = gtaiv_shader::CtabRegister(tokens, "gDirectionalColour", gtaiv_shader::kFloatSet);
  return c >= 0 && c < 224 ? uint32_t(c) : 224u;
}

// The registers of the game's own cascade lookup the patch took out (for
// the renderer's hybrid trace, gfx::RayShadow::cascade_*): the vanilla
// shaders compute, from the point's world position W,
//   dot(c14.xyz, W)                         the view depth (gViewInverse's
//                                           third row)
//   s = (-depth - c54.xyz >= 0)             the cascade (gFacetCentre)
//   scale = (s4 . c57, s4 . c58), offset = (s4 . c59, s4 . c56), s4 = (1, s)
//                                           (gShadowParam0123, 4567, 891113,
//                                           14151617)
//   W.x c60.xyw + W.y c61.xyw + W.z c62.xyw + c63.xyw
//                                           light space (gShadowMatrix)
//   the map at xy scale + offset, lit where z >= it; faded out toward
//   c53.w from the camera (gShadowParam18192021).
// False unless the CTAB names all of them.
struct CascadeRegisters {
  uint32_t sampler = 0, view = 0, split = 0, matrix = 0, range = 0;
  uint32_t select[4] = {};
};
inline bool SunShadowCascade(const std::vector<uint32_t>& tokens, CascadeRegisters* out) {
  using gtaiv_shader::CtabRegister, gtaiv_shader::kFloatSet;
  const int sampler = detail::CtabSampler(tokens, "gShadowZSamplerDir"),
            view = CtabRegister(tokens, "gViewInverse", kFloatSet),
            split = CtabRegister(tokens, "gFacetCentre", kFloatSet),
            matrix = CtabRegister(tokens, "gShadowMatrix", kFloatSet),
            range = CtabRegister(tokens, "gShadowParam18192021", kFloatSet);
  const int select[4] = {CtabRegister(tokens, "gShadowParam0123", kFloatSet),
                         CtabRegister(tokens, "gShadowParam4567", kFloatSet),
                         CtabRegister(tokens, "gShadowParam891113", kFloatSet),
                         CtabRegister(tokens, "gShadowParam14151617", kFloatSet)};
  if (sampler < 0 || sampler >= 16 || view < 0 || split < 0 || matrix < 0 || range < 0) return false;
  for (int r : select)
    if (r < 0) return false;
  out->sampler = uint32_t(sampler);
  out->view = uint32_t(view + 2);
  out->split = uint32_t(split);
  out->matrix = uint32_t(matrix);
  out->range = uint32_t(range);
  for (int k = 0; k < 4; k++) out->select[k] = uint32_t(select[k]);
  return true;
}
// The largest texel (metres) of a cascade whose answer "lit for sure" the
// renderer takes in place of a ray: casters thinner than about a third of
// it may miss every texel's centre (a pole's shadow dashed).
inline constexpr float kCascadeMaxTexel = 0.1f;
// The least distance along the light (metres) of a caster the renderer
// lets the cascades call a shadow by alone: a building's, above trees'
// canopies (whose fine gaps the cascades' texels cannot resolve; the rays
// keep them).
inline constexpr float kCascadeShadowGap = 40.0f;

// Whether `tokens` is a ps_3_0 pixel shader whose CTAB names
// gShadowZSamplerDir at s15 and GBufferTextureSampler3 at s4.
inline bool IsSunPassShader(const std::vector<uint32_t>& tokens) {
  return tokens.size() > 1 && tokens[0] == 0xFFFF0300u &&
         detail::CtabSampler(tokens, "gShadowZSamplerDir") == int(kMaskSampler) &&
         detail::CtabSampler(tokens, "GBufferTextureSampler3") == int(kDepthSampler);
}

// The sun pass's shadow lookup replaced by the mask's (above). False, the
// tokens untouched, unless the shader is one of the game's: the CTAB as
// IsSunPassShader says, exactly kTaps texld from s15, all between the
// block's first and last instructions, which come once each, in order.
inline bool PatchSunShadowLookup(std::vector<uint32_t>& tokens) {
  if (!IsSunPassShader(tokens)) return false;
  std::vector<size_t> at;
  if (!detail::Instructions(tokens, &at)) return false;
  size_t start = 0, end = 0, starts = 0, ends = 0, taps = 0, inside = 0;
  for (size_t i : at) {
    if (detail::Matches(tokens, i, kStart, 5)) {
      start = i;
      starts++;
    } else if (detail::Matches(tokens, i, kEnd, 5)) {
      end = i;
      ends++;
    }
    if (detail::TapS15(tokens, i)) {
      taps++;
      if (starts == 1 && ends == 0) inside++;
    }
  }
  if (starts != 1 || ends != 1 || end <= start || taps != kTaps || inside != kTaps) return false;
  tokens.erase(tokens.begin() + std::ptrdiff_t(start), tokens.begin() + std::ptrdiff_t(end + 5));
  tokens.insert(tokens.begin() + std::ptrdiff_t(start), std::begin(kMaskRead), std::end(kMaskRead));
  return true;
}

// Whether `tokens` is a shader PatchSunShadowLookup made: the sun pass's
// CTAB, the mask's read and no tap of s15 left.
inline bool IsPatchedSunShader(const std::vector<uint32_t>& tokens) {
  if (!IsSunPassShader(tokens)) return false;
  std::vector<size_t> at;
  if (!detail::Instructions(tokens, &at)) return false;
  size_t reads = 0, taps = 0;
  for (size_t i : at) {
    if (detail::Matches(tokens, i, kMaskRead, 7)) reads++;
    if (detail::TapS15(tokens, i)) taps++;
  }
  return reads == 1 && taps == 1;  // (the mask's read is the one tap)
}

}  // namespace gtaiv_sun_shadow
