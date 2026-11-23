// Ray-traced sun shadows in the game's forward-drawn pixel shaders: every
// pixel shader whose CTAB names gShadowZSamplerDir at s15 and that still
// reads the cascaded shadow maps there after the sun pass's patch
// (sun_shadow_tokens.h): the forward techniques (draw, drawskinned,
// lightweight0/4, draw_inst, reflection_draw, water) of the decals, glass,
// fences, hair, people, vehicles, particles' meshes and the rest, and
// gbufferDepthCopy's shadow pass (deferred_lighting.fxc pixel shader 36).
// Their cascade lookup goes; in its place one read of s15 at the point's
// world position, which the translation makes a ray query toward the sun
// (x86recomp's gfx::RayForwardShadow, hlsl::RayShadowCall: 1 lit, 0 in
// shadow; lit where the GPU cannot trace from render pipelines). A ray of
// its own, not the screen's traced mask: the decals' and posters' pixels
// lie on what the main view's depth holds, but blended and cut-out
// geometry (glass, fences, hair, smoke) is not in it, and the same pixel
// shaders serve both (gta_normal_spec.fxc's pixel shader 1 is
// gta_normal_spec_decal.fxc's, bit for bit). Used by shadows_lights.cpp
// (the game's shader hooks) and tests/forward_shadow_test.cpp.
//
// The vanilla shaders compute (gta_decal_dirt.fxc pixel shader 9; the
// others name other registers, the six folders allocate them differently):
//   mul r2.xyz, c61.xyww, v4.y      the point (v4, TEXCOORD6) through
//   mad r2.xyz, v4.x, c60.xyww, r2  gShadowMatrix (c60-c63, one row an
//   mad r2.xyz, v4.z, c62.xyww, r2  instruction, a scalar of the point
//   add r2.xyz, r2, c63.xyww        each: x, y, z)
//   ... the cascade picked (gFacetCentre), 12 Poisson taps
//   texld r6, ..., s15; add r3.z, r2.z, -r6.x; cmp r3.z, r3.z, 1, 0
//   ... summed, faded out with the distance from the camera ...
//   cmp_sat r2.x, r2.z, r2.x, r2.y  S: 1 lit, 0 in shadow
// and S scales the sun's light. The water (water.fxc pixel shader 2) takes
// its point's x and y from v2 and z from a temp; gbufferDepthCopy's pass
// makes its point from the depth, takes one tap and writes S to oC0.w
// with a cmp.
//
// PatchForwardShadowLookup finds S by data flow, whatever the registers:
// the last cmp after the last tap of s15 whose single destination
// component is then the only live value that depends on any tap (the taps
// feed nothing else). The point P is the three scalars the rows of
// gShadowMatrix are multiplied by. It adds, with F the first temp the
// shader does not use, before each row's instruction
//   mov F.<row>, <its scalar>       (x, y, z: P as it is there)
// after the last of them
//   texld F, F.xyzz, s15            the traced shadow at P
// and replaces S's instruction by
//   mov S, F.x                      (its destination, modifiers kept)
// then removes the instructions whose results nothing reads any more (the
// taps, the cascade's choice, the fade; dead code by liveness per
// component, every value live at flow control). The light the ray goes
// toward is the CTAB's gDirectionalLight (c17 in every shader that names
// it, and c17 is nothing else in any pixel shader of the game:
// gbufferDepthCopy's pass does not name it and reads the c17 the lighting
// set), pointing away from the sun as the sun pass's (sun_shadow_tokens.h).
//
// Refused (the tokens untouched) without that shape: a relative address
// or predicate anywhere, flow control between the first row and S or
// around them, an opcode the liveness does not know, no free temp, or
// another read of s15 left after the removal.

#pragma once

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "runtime/gfx/device.h"
#include "shader_ctab.h"
#include "sun_shadow_tokens.h"

namespace gtaiv_forward_shadow {

// Where the patched shaders read the traced shadow (a ray query, not a
// texture): gShadowZSamplerDir's sampler.
inline constexpr uint32_t kSampler = 15;
// The light's register where the CTAB names no gDirectionalLight.
inline constexpr uint32_t kLightRegister = gtaiv_sun_shadow::kLightRegister;
// What a shader that still reads the cascade maps compares as lit, for
// the debug view that binds a lit map in their place (gfx::RayShadow::
// own_map_lit): every tap is "add d, z, -tap.x; cmp d, d, 1, 0", lit where
// the point's depth z is at least the tap's (but gbufferDepthCopy's of the
// nv6 and nv7 folders, which decodes a packed depth first; patched).
inline constexpr float kLitTap = -65504.0f;

namespace detail {

using gtaiv_sun_shadow::detail::RegType;

enum Op : uint32_t {
  kNop = 0, kMov = 1, kAdd = 2, kSub = 3, kMad = 4, kMul = 5, kRcp = 6, kRsq = 7, kDp3 = 8, kDp4 = 9, kMin = 10,
  kMax = 11, kSlt = 12, kSge = 13, kExp = 14, kLog = 15, kLit = 16, kDst = 17, kLrp = 18, kFrc = 19, kM4x4 = 20,
  kM4x3 = 21, kM3x4 = 22, kM3x3 = 23, kM3x2 = 24, kCall = 25, kCallnz = 26, kLoop = 27, kRet = 28, kEndloop = 29,
  kLabel = 30, kDcl = 31, kPow = 32, kCrs = 33, kSgn = 34, kAbs = 35, kNrm = 36, kSincos = 37, kRep = 38,
  kEndrep = 39, kIf = 40, kIfc = 41, kElse = 42, kEndif = 43, kBreak = 44, kBreakc = 45, kMova = 46, kDefb = 47,
  kDefi = 48, kTexkill = 65, kTex = 66, kExpp = 78, kLogp = 79, kCnd = 80, kDef = 81, kCmp = 88, kDp2add = 90,
  kDsx = 91, kDsy = 92, kTexldd = 93, kSetp = 94, kTexldl = 95, kBreakp = 96,
};

constexpr uint32_t kTemp = 0, kConst = 2, kColorOut = 8, kDepthOut = 9, kSamplerType = 10;
// Components tracked: temps r0-r31 (4 r + c), then the outputs oC0-oC3
// and oDepth, which stay live.
constexpr size_t kTemps = 32, kOutputs = 128, kComps = 160;
using Comps = std::bitset<kComps>;

struct Ins {
  size_t at;     // the instruction token
  uint32_t op;
  size_t n;      // parameter tokens
};

inline uint32_t Num(uint32_t r) { return r & 0x7FF; }
inline uint32_t Mask(uint32_t d) { return (d >> 16) & 0xF; }
inline uint32_t Swz(uint32_t s, uint32_t c) { return (s >> (16 + 2 * c)) & 3; }

inline bool Flow(uint32_t op) {
  switch (op) {
    case kCall: case kCallnz: case kLoop: case kRet: case kEndloop: case kLabel: case kRep: case kEndrep:
    case kIf: case kIfc: case kElse: case kEndif: case kBreak: case kBreakc: case kBreakp:
      return true;
    default:
      return false;
  }
}
inline bool Skipped(uint32_t op) { return op == kNop || op == kDcl || op == kDef || op == kDefi || op == kDefb; }
inline bool HasDst(uint32_t op) { return !Skipped(op) && !Flow(op) && op != kTexkill; }
inline bool IsRead(uint32_t op) { return op == kTex || op == kTexldl || op == kTexldd; }

// The instructions (comments skipped); false if the stream is cut short.
inline bool Parse(const std::vector<uint32_t>& t, std::vector<Ins>* out) {
  out->clear();
  for (size_t i = 1; i < t.size();) {
    const uint32_t op = t[i] & 0xFFFF;
    if (op == 0xFFFF) return true;
    const size_t n = op == 0xFFFE ? (t[i] >> 16) & 0x7FFF : (t[i] >> 24) & 0xF;
    if (i + 1 + n > t.size()) return false;
    if (op != 0xFFFE) out->push_back({i, op, n});
    i += 1 + n;
  }
  return false;
}

// A temp or output component's index; kComps if not tracked.
inline size_t Comp(uint32_t r, uint32_t c) {
  const uint32_t type = RegType(r), n = Num(r);
  if (type == kTemp && n < kTemps) return n * 4 + c;
  if (type == kColorOut && n < 4) return kOutputs + n * 4 + c;
  if (type == kDepthOut) return kOutputs + 16;
  return kComps;
}

// A read of sampler s at instruction `in`.
inline bool ReadsSampler(const std::vector<uint32_t>& t, const Ins& in, uint32_t s) {
  return IsRead(in.op) && in.n >= 3 && RegType(t[in.at + 3]) == kSamplerType && Num(t[in.at + 3]) == s;
}

// The temp components instruction `in` reads (given its write mask); false
// for an opcode it does not know.
inline bool Reads(const std::vector<uint32_t>& t, const Ins& in, Comps* r) {
  r->reset();
  if (Skipped(in.op) || in.n == 0) return true;
  auto add = [&](uint32_t s, uint32_t comps) {  // the swizzled components 0..comps-1
    for (uint32_t c = 0; c < comps; c++)
      if (const size_t k = Comp(s, Swz(s, c)); k < kOutputs) r->set(k);
  };
  if (in.op == kTexkill) {  // a destination-shaped parameter: its mask
    const uint32_t d = t[in.at + 1];
    for (uint32_t c = 0; c < 4; c++)
      if (Mask(d) >> c & 1)
        if (const size_t k = Comp(d, c); k < kOutputs) r->set(k);
    return true;
  }
  if (Flow(in.op)) {
    for (size_t k = 1; k <= in.n; k++) add(t[in.at + k], 4);
    return true;
  }
  const uint32_t mask = Mask(t[in.at + 1]);
  for (size_t k = 2; k <= in.n; k++) {
    const uint32_t s = t[in.at + k];
    if (RegType(s) == kSamplerType) continue;
    switch (in.op) {
      case kMov: case kAdd: case kSub: case kMad: case kMul: case kMin: case kMax: case kSlt: case kSge:
      case kLrp: case kFrc: case kCmp: case kCnd: case kAbs: case kDsx: case kDsy: case kSgn: case kSetp:
        for (uint32_t c = 0; c < 4; c++)
          if (mask >> c & 1)
            if (const size_t q = Comp(s, Swz(s, c)); q < kOutputs) r->set(q);
        break;
      case kDp3: case kNrm: case kCrs:
        add(s, 3);
        break;
      case kDp2add:
        add(s, k < 4 ? 2 : 4);
        break;
      case kRcp: case kRsq: case kExp: case kLog: case kExpp: case kLogp: case kPow: case kDp4: case kLit:
      case kDst: case kSincos: case kTex: case kTexldl: case kTexldd: case kMova:
        add(s, 4);
        break;
      case kM4x4: case kM4x3: case kM3x4: case kM3x3: case kM3x2: {
        add(s, 4);
        if (k == 3 && RegType(s) == kTemp) {  // a matrix of temps: its rows
          const uint32_t rows = in.op == kM4x4 || in.op == kM3x4 ? 4 : in.op == kM3x2 ? 2 : 3;
          for (uint32_t row = 0; row < rows; row++)
            for (uint32_t c = 0; c < 4; c++)
              if (Num(s) + row < kTemps) r->set((Num(s) + row) * 4 + c);
        }
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

// The temp and output components it writes.
inline Comps Writes(const std::vector<uint32_t>& t, const Ins& in) {
  Comps w;
  if (!HasDst(in.op) || in.n == 0) return w;
  const uint32_t d = t[in.at + 1];
  for (uint32_t c = 0; c < 4; c++)
    if (Mask(d) >> c & 1)
      if (const size_t k = Comp(d, c); k < kComps) w.set(k);
  return w;
}

inline bool Predicated(const std::vector<uint32_t>& t, const Ins& in) { return (t[in.at] >> 28) & 1; }

// Whether any parameter is relative-addressed or any instruction predicated.
inline bool Plain(const std::vector<uint32_t>& t, const std::vector<Ins>& is) {
  for (const Ins& in : is) {
    if (Skipped(in.op)) continue;
    if (Predicated(t, in)) return false;
    for (size_t k = 1; k <= in.n; k++)
      if (t[in.at + k] & 0x2000) return false;
  }
  return true;
}

// Live components after each instruction (outputs always), every one live
// at flow control; false for an opcode Reads does not know.
inline bool Liveness(const std::vector<uint32_t>& t, const std::vector<Ins>& is, std::vector<Comps>* after) {
  after->assign(is.size(), Comps());
  Comps live, outputs;
  for (size_t k = kOutputs; k < kComps; k++) outputs.set(k);
  live = outputs;
  for (size_t k = is.size(); k-- > 0;) {
    (*after)[k] = live;
    const Ins& in = is[k];
    Comps r;
    if (!Reads(t, in, &r)) return false;
    if (Flow(in.op)) {
      live.set();
      continue;
    }
    if (!Predicated(t, in)) live &= ~Writes(t, in);
    live |= r | outputs;
  }
  return true;
}

// Instructions removed whose results nothing reads (temps only, no texkill
// or flow control): one pass back from the end, a removed instruction's
// reads not made live (every component live at flow control); false for
// an opcode Reads does not know.
inline bool RemoveDead(std::vector<uint32_t>& t) {
  std::vector<Ins> is;
  if (!Parse(t, &is)) return false;
  Comps live, outputs;
  for (size_t k = kOutputs; k < kComps; k++) outputs.set(k);
  live = outputs;
  std::vector<bool> dead(is.size(), false);
  bool any = false;
  for (size_t k = is.size(); k-- > 0;) {
    const Ins& in = is[k];
    Comps r;
    if (!Reads(t, in, &r)) return false;
    if (Flow(in.op)) {
      live.set();
      continue;
    }
    const Comps w = Writes(t, in);
    const bool pred = Predicated(t, in);
    if (HasDst(in.op) && !pred && RegType(t[in.at + 1]) == kTemp && (w & live).none()) {
      dead[k] = any = true;
      continue;
    }
    if (!pred) live &= ~w;
    live |= r | outputs;
  }
  if (!any) return true;
  std::vector<uint32_t> out;
  out.reserve(t.size());
  size_t from = 0;
  for (size_t k = 0; k < is.size(); k++) {
    if (!dead[k]) continue;
    out.insert(out.end(), t.begin() + std::ptrdiff_t(from), t.begin() + std::ptrdiff_t(is[k].at));
    from = is[k].at + 1 + is[k].n;
  }
  out.insert(out.end(), t.begin() + std::ptrdiff_t(from), t.end());
  t = std::move(out);
  return true;
}

inline int CtabSampler(const std::vector<uint32_t>& t, const char* name) {
  return gtaiv_shader::CtabRegister(t, name, gtaiv_shader::kSamplerSet);
}
inline int CtabFloat(const std::vector<uint32_t>& t, const char* name) {
  return gtaiv_shader::CtabRegister(t, name, gtaiv_shader::kFloatSet);
}

// Whether `t` is a ps_3_0 pixel shader whose CTAB names gShadowZSamplerDir
// at s15, other than the sun pass's.
inline bool Candidate(const std::vector<uint32_t>& t) {
  return t.size() > 1 && t[0] == 0xFFFF0300u && CtabSampler(t, "gShadowZSamplerDir") == int(kSampler) &&
         !gtaiv_sun_shadow::IsSunPassShader(t);
}

// The reads of s15 left.
inline size_t S15Reads(const std::vector<uint32_t>& t, const std::vector<Ins>& is) {
  size_t n = 0;
  // (A texldd of it is not a read of the maps: forward_light_tokens.h's ray.)
  for (const Ins& in : is) n += ReadsSampler(t, in, kSampler) && in.op != kTexldd ? 1 : 0;
  return n;
}

// The shape's anchors: the rows' instructions (with the point's scalar
// sources) and S's.
struct Shape {
  size_t row[3] = {};       // instruction indices
  uint32_t scalar[3] = {};  // the point's source token at each
  size_t s = 0;             // S's instruction index
  uint32_t temp = 0;        // F
};

inline bool Find(const std::vector<uint32_t>& t, const std::vector<Ins>& is, Shape* sh) {
  const int m = CtabFloat(t, "gShadowMatrix");
  if (m < 0 || !Plain(t, is)) return false;
  // The rows: one instruction each reading c<m + row> (rows 0-2: a mul or
  // mad of it by a scalar of the point, x, y, z), the taps, the temps used.
  int rows[4] = {-1, -1, -1, -1};
  size_t last_tap = 0, taps = 0;
  uint32_t temps = 0;
  for (size_t k = 0; k < is.size(); k++) {
    const Ins& in = is[k];
    if (Skipped(in.op)) continue;
    for (size_t p = 1; p <= in.n; p++) {
      const uint32_t r = t[in.at + p];
      if (RegType(r) == kTemp) temps = std::max(temps, Num(r) + 1);
      if (p >= 2 && RegType(r) == kConst && Num(r) >= uint32_t(m) && Num(r) < uint32_t(m) + 4) {
        int& row = rows[Num(r) - uint32_t(m)];
        if (row >= 0 && row != int(k)) return false;
        row = int(k);
      }
    }
    if (ReadsSampler(t, in, kSampler)) {
      last_tap = k;
      taps++;
    }
  }
  if (!taps || temps >= kTemps) return false;
  sh->temp = temps;
  for (uint32_t row = 0; row < 3; row++) {
    if (rows[row] < 0) return false;
    const Ins& in = is[size_t(rows[row])];
    if ((in.op != kMul && in.op != kMad) || in.n < 3) return false;
    const uint32_t a = t[in.at + 2], b = t[in.at + 3];
    const bool a_row = RegType(a) == kConst && Num(a) == uint32_t(m) + row;
    const uint32_t s = a_row ? b : a;
    if (!a_row && !(RegType(b) == kConst && Num(b) == uint32_t(m) + row)) return false;
    const uint32_t type = RegType(s);
    if ((type != kTemp && type != 1) || ((s >> 24) & 0xF) != 0) return false;  // r or v, unmodified
    for (uint32_t c = 0; c < 4; c++)
      if (Swz(s, c) != row) return false;  // .x, .y, .z in turn
    sh->row[row] = size_t(rows[row]);
    sh->scalar[row] = s;
  }
  // Taint from the taps forward; S: the last cmp after the last tap that
  // writes one component, then the only live tainted one.
  std::vector<Comps> after;
  if (!Liveness(t, is, &after)) return false;
  Comps taint;
  bool found = false;
  for (size_t k = 0; k < is.size(); k++) {
    const Ins& in = is[k];
    Comps r;
    if (!Reads(t, in, &r)) return false;
    const bool tainted = ReadsSampler(t, in, kSampler) || (r & taint).any();
    const Comps w = Writes(t, in);
    if (tainted)
      taint |= w;
    else
      taint &= ~w;
    if (k <= last_tap || in.op != kCmp || w.count() != 1) continue;
    if ((taint & after[k]) == w) {
      sh->s = k;
      found = true;
    }
  }
  if (!found) return false;
  // No flow control from the first row to S, none around them.
  const size_t first = std::min({sh->row[0], sh->row[1], sh->row[2]});
  int depth = 0;
  for (size_t k = 0; k <= sh->s; k++) {
    const uint32_t op = is[k].op;
    if (k >= first && Flow(op)) return false;
    if (op == kIf || op == kIfc || op == kRep || op == kLoop) depth++;
    if (op == kEndif || op == kEndrep || op == kEndloop) depth--;
    if (op == kLabel) return false;
  }
  return depth == 0 && first < sh->s;
}

}  // namespace detail

// The cascade lookup replaced by the traced shadow's read (above). False,
// the tokens untouched, unless the shader is in that shape.
inline bool PatchForwardShadowLookup(std::vector<uint32_t>& tokens) {
  using namespace detail;
  if (!Candidate(tokens)) return false;
  std::vector<Ins> is;
  if (!Parse(tokens, &is)) return false;
  Shape sh;
  if (!Find(tokens, is, &sh)) return false;
  const uint32_t f = sh.temp;
  const size_t last_row = std::max({sh.row[0], sh.row[1], sh.row[2]});
  std::vector<uint32_t> out(tokens.begin(), tokens.begin() + std::ptrdiff_t(is[0].at));
  for (size_t k = 0; k < is.size(); k++) {
    const Ins& in = is[k];
    const size_t end = k + 1 < is.size() ? is[k + 1].at : tokens.size();  // (comments between kept)
    for (uint32_t row = 0; row < 3; row++)
      if (sh.row[row] == k) out.insert(out.end(), {0x02000001u, 0x80000000u | (1u << (16 + row)) | f, sh.scalar[row]});
    if (k == sh.s) {
      out.insert(out.end(), {0x02000001u, tokens[in.at + 1], 0x80000000u | f});  // mov S, F.x
      out.insert(out.end(), tokens.begin() + std::ptrdiff_t(in.at + 1 + in.n), tokens.begin() + std::ptrdiff_t(end));
    } else {
      out.insert(out.end(), tokens.begin() + std::ptrdiff_t(in.at), tokens.begin() + std::ptrdiff_t(end));
    }
    if (k == last_row)  // texld F, F.xyzz, s15
      out.insert(out.end(), {0x03000042u, 0x800F0000u | f, 0x80A40000u | f, 0xA0E4080Fu});
  }
  if (!RemoveDead(out) || !Parse(out, &is) || S15Reads(out, is) != 1) return false;
  tokens = std::move(out);
  return true;
}

// Whether `tokens` is a shader PatchForwardShadowLookup made: such a CTAB,
// and its one read of s15 "texld F, F.xyzz, s15".
inline bool IsPatchedForwardShadow(const std::vector<uint32_t>& tokens) {
  using namespace detail;
  if (!Candidate(tokens)) return false;
  std::vector<Ins> is;
  if (!Parse(tokens, &is) || S15Reads(tokens, is) != 1) return false;
  for (const Ins& in : is) {
    if (!ReadsSampler(tokens, in, kSampler)) continue;
    const uint32_t d = tokens[in.at + 1], s = tokens[in.at + 2];
    return in.op == kTex && ((tokens[in.at] >> 16) & 0xFF) == 0 && RegType(d) == kTemp && Mask(d) == 0xF &&
           ((d >> 20) & 0xF) == 0 && s == (0x80A40000u | Num(d));
  }
  return false;
}

// The consumer's answer (GameConfig::ray_forward_shadow_consumer) for a
// shader PatchForwardShadowLookup made: the read at s15, the light's
// register (pointing away from the sun).
inline bool ForwardShadowConsumer(const std::vector<uint32_t>& tokens, recomp::gfx::RayForwardShadow* out) {
  if (!IsPatchedForwardShadow(tokens)) return false;
  const int light = detail::CtabFloat(tokens, "gDirectionalLight");
  out->sampler = kSampler;
  out->light_register = light >= 0 ? uint32_t(light) : kLightRegister;
  out->toward_light = false;
  return true;
}

// Whether `tokens` still reads the cascade maps: a ps_3_0 shader whose
// CTAB names gShadowZSamplerDir at s15 that reads s15 and is neither
// patch's (the sun pass's, PatchSunShadowLookup; the forward ones,
// PatchForwardShadowLookup): what the patches could not take.
inline bool ReadsCascadeMaps(const std::vector<uint32_t>& tokens) {
  using namespace detail;
  if (tokens.size() < 2 || tokens[0] != 0xFFFF0300u || CtabSampler(tokens, "gShadowZSamplerDir") != int(kSampler))
    return false;
  if (gtaiv_sun_shadow::IsPatchedSunShader(tokens) || IsPatchedForwardShadow(tokens)) return false;
  std::vector<Ins> is;
  if (!Parse(tokens, &is)) return true;  // (cut short: say so)
  return S15Reads(tokens, is) > 0;
}

}  // namespace gtaiv_forward_shadow
