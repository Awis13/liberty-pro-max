// Ray-traced shadows for the local lights of the game's forward-drawn pixel
// shaders (people, vehicles, glass, decals and the rest of the forward
// techniques): their lamps' light reaches a pixel only where a ray from it
// to the lamp is free. Used by shadows_lights.cpp (the game's shader hooks)
// and tests/forward_light_test.cpp.
//
// The vanilla shaders light a pixel with up to four lights a set, one lane
// a light (gta_ped.fxc pixel shader 4; other shaders name other registers):
//   add r5, c19, -v7.x      gLightPosX - P.x   (to the lights, four lanes)
//   add r6, c20, -v7.y      gLightPosY - P.y
//   add r7, c21, -v7.z      gLightPosZ - P.z
//   ...                     falloff, N.L, the cone: r8, a weight a light
//   dp4 r11.x, c29, r8      gLightColR . weights
//   dp4 r11.y, c30, r8      gLightColG
//   dp4 r11.z, c31, r8      gLightColB
//   ...                     the specular term from r8 again
// and nothing shadows them. A second set of four (gLightPointPosX...,
// gLightPointColR...) has the same shape.
//
// PatchForwardLights finds, for each set the CTAB names, the three
// instructions reading the positions (their destinations L the vectors to
// the lights, the other operand the point's scalar) and the first dp4 of
// the set's red colour (its other operand W the weights). With Q, Tx, Ty,
// Tz, G, F the first temps the shader does not use (B below is F, each lane
// read before its answer is written there), it adds after each
// position's instruction
//   mov Q.<x|y|z>, <the point's scalar>
//   mov T<x|y|z>, L         (kept: the shader goes on to change L)
// and before that dp4
//   max B, <red>, <green>; max B, B, <blue>; mul B, B, W
// (what each light brings the pixel: its weight times its brightest
// channel; 0 for a lane with no light in it, as most are by day), then for
// each lane i
//   mov G.x, Tx.<i>; mov G.y, Ty.<i>; mov G.z, Tz.<i>
//   texldd G, Q.xyzz, s15, G.xyzz, B.<i>   the traced shadow toward P + G
//   mov F.<i>, G.x
// then
//   mul W, W, F
// so the diffuse and specular terms made from W are shadowed. A later dp4
// of that colour whose weights do not come from W gets its own mul. The
// texldd of s15 is what the translation makes a ray query (x86recomp's
// hlsl::RayShadowCall: 1 lit, 0 in shadow, lit untraced where its last
// operand is not above 0: no ray toward a light that brings the pixel
// nothing; and where the GPU cannot trace from render pipelines).
//
// Refused (the tokens untouched) without that shape: a relative address or
// predicate anywhere, flow control from the first position to the last
// dp4 or around them, an
// opcode the data flow does not know, no free temps, or s15 named as
// anything but the shadow maps.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "forward_shadow_tokens.h"
#include "light_shadow_tokens.h"
#include "runtime/gfx/device.h"
#include "shader_ctab.h"

namespace gtaiv_forward_light {

// Where the patched shaders ask for the traced shadow: the shadow maps'
// sampler, a texldd of it.
inline constexpr uint32_t kSampler = gtaiv_forward_shadow::kSampler;

namespace detail {

using namespace gtaiv_forward_shadow::detail;

struct Set {
  const char* pos[3];
  const char* red;
  const char* rest[2];  // green, blue
};
inline constexpr Set kSets[] = {
    {{"gLightPosX", "gLightPosY", "gLightPosZ"}, "gLightColR", {"gLightColG", "gLightColB"}},
    {{"gLightPointPosX", "gLightPointPosY", "gLightPointPosZ"}, "gLightPointColR", {"gLightPointColG", "gLightPointColB"}},
};

inline bool IsConst(uint32_t r, int num) { return RegType(r) == kConst && int(Num(r)) == num; }
inline uint32_t Modifier(uint32_t s) { return (s >> 24) & 0xF; }
// A source's swizzle names one component four times.
inline bool Scalar(uint32_t s) { return Swz(s, 0) == Swz(s, 1) && Swz(s, 1) == Swz(s, 2) && Swz(s, 2) == Swz(s, 3); }
inline bool WholeTemp(uint32_t d) { return RegType(d) == kTemp && Mask(d) == 0xF && Num(d) < kTemps; }

// A texldd of the shadow maps' sampler: what PatchForwardLights adds.
inline bool IsLightRay(const std::vector<uint32_t>& t, const Ins& in) {
  return in.op == kTexldd && in.n >= 5 && RegType(t[in.at + 3]) == kSamplerType && Num(t[in.at + 3]) == kSampler;
}

struct Shape {
  size_t pos[3] = {};       // the positions' instructions
  uint32_t scalar[3] = {};  // the point's scalar at each (unmodified)
  uint32_t to[3] = {};      // their destinations (temps)
  std::vector<size_t> dots; // the red dp4s that get a mul before them
  std::vector<uint32_t> weights;  // each one's weights (a temp)
  int colour[3] = {-1, -1, -1};  // the set's colour registers (green, blue: -1 where the shader has none)
};

// (Find's refusals numbered, for the test's report.)
inline bool Why(int* why, int n) {
  if (why) *why = n;
  return false;
}
inline bool Find(const std::vector<uint32_t>& t, const std::vector<Ins>& is, const Set& set, Shape* sh, int* why = nullptr) {
  int pos[3], red = CtabFloat(t, set.red);
  for (int k = 0; k < 3; k++) pos[k] = CtabFloat(t, set.pos[k]);
  if (red < 0 || pos[0] < 0 || pos[1] < 0 || pos[2] < 0) return Why(why, 1);
  sh->colour[0] = red;
  sh->colour[1] = CtabFloat(t, set.rest[0]);
  sh->colour[2] = CtabFloat(t, set.rest[1]);
  // The positions: "add L, c, -scalar" (or sub L, c, scalar), once each.
  for (int k = 0; k < 3; k++) {
    int at = -1;
    for (size_t i = 0; i < is.size(); i++) {
      const Ins& in = is[i];
      if (Skipped(in.op)) continue;
      bool reads = false;
      for (size_t p = 2; p <= in.n; p++) reads = reads || IsConst(t[in.at + p], pos[k]);
      if (!reads) continue;
      if (at >= 0) return Why(why, 2);
      at = int(i);
    }
    if (at < 0) return Why(why, 3);
    const Ins& in = is[size_t(at)];
    if ((in.op != kAdd && in.op != kSub) || in.n != 3 || !WholeTemp(t[in.at + 1])) return Why(why, 4);
    const uint32_t a = t[in.at + 2], b = t[in.at + 3];
    if (!IsConst(a, pos[k]) || Modifier(a) != 0 || ((a >> 16) & 0xFF) != 0xE4) return Why(why, 5);
    if (Modifier(b) != (in.op == kAdd ? 1u : 0u) || !Scalar(b)) return Why(why, 6);  // c - scalar
    const uint32_t type = RegType(b);
    if (type != kTemp && type != 1) return Why(why, 7);  // r or v
    sh->pos[k] = size_t(at);
    sh->scalar[k] = b & ~0x0F000000u;
    sh->to[k] = Num(t[in.at + 1]);
  }
  if (sh->to[0] == sh->to[1] || sh->to[1] == sh->to[2] || sh->to[0] == sh->to[2]) return Why(why, 8);
  const size_t first = std::min({sh->pos[0], sh->pos[1], sh->pos[2]});
  const size_t last_pos = std::max({sh->pos[0], sh->pos[1], sh->pos[2]});
  // The red dp4s after them; the weights a whole unmodified temp.
  std::vector<bool> shadowed(kTemps, false);
  size_t last = 0;
  for (size_t i = last_pos + 1; i < is.size(); i++) {
    const Ins& in = is[i];
    if (Skipped(in.op)) continue;
    Comps r;
    if (!Reads(t, in, &r)) return Why(why, 9);
    if (in.op == kDp4 && in.n == 3 && (IsConst(t[in.at + 2], red) || IsConst(t[in.at + 3], red))) {
      const uint32_t w = IsConst(t[in.at + 2], red) ? t[in.at + 3] : t[in.at + 2];
      if (RegType(w) != kTemp || Num(w) >= kTemps || Modifier(w) != 0 || ((w >> 16) & 0xFF) != 0xE4) return Why(why, 10);
      if (!shadowed[Num(w)]) {
        sh->dots.push_back(i);
        sh->weights.push_back(Num(w));
        shadowed[Num(w)] = true;
      }
      last = i;
    }
    // What is made from shadowed weights is shadowed; a temp written whole
    // from anything else no longer is.
    if (!HasDst(in.op) || in.n == 0 || RegType(t[in.at + 1]) != kTemp || Num(t[in.at + 1]) >= kTemps) continue;
    bool from = false;
    for (size_t c = 0; c < kOutputs; c++) from = from || (r.test(c) && shadowed[c / 4]);
    const uint32_t d = Num(t[in.at + 1]);
    if (from)
      shadowed[d] = true;
    else if (Mask(t[in.at + 1]) == 0xF && !Predicated(t, in))
      shadowed[d] = false;
  }
  if (sh->dots.empty()) return Why(why, 11);
  // No flow control from the first position to the last dp4, none around.
  int depth = 0;
  for (size_t i = 0; i <= last; i++) {
    const uint32_t op = is[i].op;
    if (i >= first && Flow(op)) return Why(why, 13);
    if (op == kIf || op == kIfc || op == kRep || op == kLoop) depth++;
    if (op == kEndif || op == kEndrep || op == kEndloop) depth--;
    if (op == kLabel) return Why(why, 14);
  }
  return depth == 0;
}

// The temps in use: one past the highest numbered.
inline uint32_t TempsUsed(const std::vector<uint32_t>& t, const std::vector<Ins>& is) {
  uint32_t temps = 0;
  for (const Ins& in : is) {
    if (Skipped(in.op)) continue;
    for (size_t p = 1; p <= in.n; p++)
      if (RegType(t[in.at + p]) == kTemp) temps = std::max(temps, Num(t[in.at + p]) + 1);
  }
  return temps;
}

// s15 is the shadow maps' sampler or nothing's: no other sampler of the
// CTAB's sits there and, unnamed, no instruction reads it.
inline bool SamplerFree(const std::vector<uint32_t>& t, const std::vector<Ins>& is) {
  if (CtabSampler(t, "gShadowZSamplerDir") == int(kSampler)) return true;
  for (const Ins& in : is) {
    if (in.op == kDcl && in.n >= 2 && RegType(t[in.at + 2]) == kSamplerType && Num(t[in.at + 2]) == kSampler)
      return false;
    if (ReadsSampler(t, in, kSampler)) return false;
  }
  return true;
}

inline bool PatchSet(std::vector<uint32_t>& tokens, const Set& set, int* why = nullptr, bool half = false) {
  std::vector<Ins> is;
  if (!Parse(tokens, &is)) return Why(why, 101);
  if (!Plain(tokens, is)) return Why(why, 102);
  if (!SamplerFree(tokens, is)) return Why(why, 103);
  Shape sh;
  if (!Find(tokens, is, set, &sh, why)) return false;
  // Q the point, T the vectors to the lights (kept: the shader goes on to
  // change its own), G a lane's vector and the ray's answer, F the four
  // answers; before a lane's answer, its lane of F holds what the light
  // brings the pixel (B above: no temp of its own).
  const uint32_t q = TempsUsed(tokens, is), tv = q + 1, g = q + 4, f = q + 5, b = f;
  if (f >= kTemps) return Why(why, 104);
  const auto colour = [&](int c) { return 0xA0E40000u | uint32_t(sh.colour[c]); };
  const auto lane = [](uint32_t i) { return (i * 0x55u) << 16; };
  std::vector<uint32_t> out(tokens.begin(), tokens.begin() + std::ptrdiff_t(is[0].at));
  for (size_t k = 0; k < is.size(); k++) {
    const Ins& in = is[k];
    const size_t end = k + 1 < is.size() ? is[k + 1].at : tokens.size();  // (comments between kept)
    for (size_t d = 0; d < sh.dots.size(); d++) {
      if (sh.dots[d] != k) continue;
      const uint32_t w = sh.weights[d];
      if (d == 0) {
        // B = W * max(red, green, blue), a lane a light (a colour the
        // shader does not name counts as the red).
        out.insert(out.end(), {0x0300000Bu, 0x800F0000u | b, colour(0), colour(sh.colour[1] >= 0 ? 1 : 0)});
        out.insert(out.end(), {0x0300000Bu, 0x800F0000u | b, 0x80E40000u | b, colour(sh.colour[2] >= 0 ? 2 : 0)});
        out.insert(out.end(), {0x03000005u, 0x800F0000u | b, 0x80E40000u | b, 0x80E40000u | w});
        // Only the strongest of the four keeps its weight (the others 0:
        // no ray, lit as the game lights them); with `half`, those that
        // bring at least half of what it does. Interiors and the subway
        // draw their walls with these shaders, up to eight lamps a pixel
        // alike (a station's tubes all pass the half): their rays cost
        // ~10 ms a frame at City Hall station (31.5 fps without them, 24
        // with). With G free before the lanes:
        //   max G.x, B.x, B.y; max G.x, G.x, B.z; max G.x, G.x, B.w
        //   a lane i: mov G.y, B.i (half: add G.y, B.i, B.i);
        //             add G.y, G.y, -G.x;
        //             add G.z, B.i, -B.i; cmp B.i, G.y, B.i, G.z
        const auto rep = [&](uint32_t r, uint32_t i, bool neg = false) {
          return 0x80000000u | lane(i) | r | (neg ? 0x01000000u : 0u);
        };
        const auto dst = [](uint32_t r, uint32_t c) { return 0x80000000u | (1u << (16 + c)) | r; };
        out.insert(out.end(), {0x0300000Bu, dst(g, 0), rep(b, 0), rep(b, 1)});
        out.insert(out.end(), {0x0300000Bu, dst(g, 0), rep(g, 0), rep(b, 2)});
        out.insert(out.end(), {0x0300000Bu, dst(g, 0), rep(g, 0), rep(b, 3)});
        for (uint32_t i = 0; i < 4; i++) {
          if (half)
            out.insert(out.end(), {0x03000002u, dst(g, 1), rep(b, i), rep(b, i)});
          else
            out.insert(out.end(), {0x02000001u, dst(g, 1), rep(b, i)});
          out.insert(out.end(), {0x03000002u, dst(g, 1), rep(g, 1), rep(g, 0, true)});
          out.insert(out.end(), {0x03000002u, dst(g, 2), rep(b, i), rep(b, i, true)});
          out.insert(out.end(), {0x04000058u, dst(b, i), rep(g, 1), rep(b, i), rep(g, 2)});
        }
        for (uint32_t i = 0; i < 4; i++) {
          for (uint32_t c = 0; c < 3; c++)  // mov G.<c>, L<c>.<i>
            out.insert(out.end(), {0x02000001u, 0x80000000u | (1u << (16 + c)) | g, 0x80000000u | lane(i) | (tv + c)});
          // texldd G, Q.xyzz, s15, G.xyzz, B.<i>
          out.insert(out.end(), {0x0500005Du, 0x800F0000u | g, 0x80A40000u | q, 0xA0E40800u | kSampler,
                                 0x80A40000u | g, 0x80000000u | lane(i) | b});
          out.insert(out.end(), {0x02000001u, 0x80000000u | (1u << (16 + i)) | f, 0x80000000u | g});  // mov F.<i>, G.x
        }
      }
      out.insert(out.end(), {0x03000005u, 0x800F0000u | w, 0x80E40000u | w, 0x80E40000u | f});  // mul W, W, F
    }
    out.insert(out.end(), tokens.begin() + std::ptrdiff_t(in.at), tokens.begin() + std::ptrdiff_t(end));
    for (uint32_t c = 0; c < 3; c++)
      if (sh.pos[c] == k) {  // mov Q.<c>, the point's scalar; mov T<c>, L<c>
        out.insert(out.end(), {0x02000001u, 0x80000000u | (1u << (16 + c)) | q, sh.scalar[c]});
        out.insert(out.end(), {0x02000001u, 0x800F0000u | (tv + c), 0x80E40000u | sh.to[c]});
      }
  }
  tokens = std::move(out);
  return true;
}

}  // namespace detail

// The local lights' weights times their traced shadows (above), for each
// set of lights the shader has: a ray toward a set's strongest light only,
// or with `half` toward each bringing half of what it does. False, the
// tokens untouched, if no set is in that shape.
inline bool PatchForwardLights(std::vector<uint32_t>& tokens, bool half = false) {
  if (tokens.size() < 2 || tokens[0] != 0xFFFF0300u) return false;
  bool any = false;
  for (const detail::Set& set : detail::kSets) any = detail::PatchSet(tokens, set, nullptr, half) || any;
  return any;
}

// Whether `tokens` asks for a traced shadow toward a local light (a shader
// PatchForwardLights made).
inline bool HasForwardLightRays(const std::vector<uint32_t>& tokens) {
  using namespace detail;
  if (tokens.size() < 2 || tokens[0] != 0xFFFF0300u) return false;
  std::vector<Ins> is;
  if (!Parse(tokens, &is)) return false;
  for (const Ins& in : is)
    if (IsLightRay(tokens, in)) return true;
  return false;
}

// The consumer's answer (GameConfig::ray_forward_shadow_consumer) for such
// a shader: the sun's read as forward_shadow_tokens.h gives it where the
// shader has it too, the lamps' rays ending the lamp's gap short of them.
inline bool ForwardLightsConsumer(const std::vector<uint32_t>& tokens, recomp::gfx::RayForwardShadow* out) {
  if (!HasForwardLightRays(tokens)) return false;
  if (!gtaiv_forward_shadow::ForwardShadowConsumer(tokens, out)) {
    *out = recomp::gfx::RayForwardShadow{};
    out->sampler = kSampler;
    out->light_register = gtaiv_forward_shadow::kLightRegister;
    out->toward_light = false;
  }
  out->lamp_gap = gtaiv_light_shadow::kLampGap;
  return true;
}

}  // namespace gtaiv_forward_light
