// The lighting's reflection term blended with the traced reflections
// (src/reflection_tokens.h). Always: a synthetic ps_3_0 shaped as the game's
// (patched as expected: the blend after the paraboloid's texldl, dcl_2d s3
// after the other declarations; taken by the consumer's check, not patched
// twice; the consumer's answer for it only) and left alone where r8 or s3
// is used, the paraboloid is read twice or by texld, the muls are not the
// fades' or the first writes the second's fade, the CTAB lacks a name the
// consumer reads, or it is a vertex shader. With GTAIV_GAME_DIR set (the
// install): the game's own deferred_lighting.fxc of each of the six shader
// folders (all must be there): pixel shaders 1-4 patched and no other, each
// once (one dcl_2d s3, one read of s3, the texldl kept, 20 tokens more), the
// consumer's answer for them and not for the originals (s3; s4, s1, s2;
// c17, c18, c37, c38, c72; wet with a stencil at s5 in 2 and 4 only), the
// term times a def's 10
// after the texldl (the output's scale, 1 / 10), and the result translates
// (hlsl::TranslateBytecode); pixel shaders 1 and 2 also with the sun
// shadow's patch (sun_shadow_tokens.h), in either order, to the same tokens;
// skipped without it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "reflection_tokens.h"
#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace gr = gtaiv_reflection;
namespace ss = gtaiv_sun_shadow;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

struct Entry {
  const char* name;
  uint16_t set, reg;
};

// A CTAB comment naming `entries`.
Tokens Ctab(std::initializer_list<Entry> entries) {
  const size_t n = entries.size();
  std::vector<uint8_t> t(28 + n * 20, 0);
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&t[at], &v, 4); };
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&t[at], &v, 2); };
  auto str = [&](const char* s) {
    const uint32_t at = uint32_t(t.size());
    t.insert(t.end(), s, s + std::strlen(s) + 1);
    return at;
  };
  put32(0, 28);
  put32(8, 0xFFFF0300u);
  put32(12, uint32_t(n));
  put32(16, 28);
  size_t i = 0;
  for (const Entry& e : entries) {
    const uint32_t name = str(e.name);
    put32(28 + i * 20, name);
    put16(28 + i * 20 + 4, e.set);
    put16(28 + i * 20 + 6, e.reg);
    put16(28 + i * 20 + 8, 1);
    i++;
  }
  put32(4, str("test"));
  put32(24, str("ps_3_0"));
  while (t.size() % 4) t.push_back(0);
  Tokens out;
  out.push_back(0xFFFEu | uint32_t(t.size() / 4 + 1) << 16);
  out.push_back(0x42415443u);  // 'CTAB'
  for (size_t k = 0; k < t.size(); k += 4) {
    uint32_t v;
    std::memcpy(&v, &t[k], 4);
    out.push_back(v);
  }
  return out;
}

void Add(Tokens& t, std::initializer_list<uint32_t> v) { t.insert(t.end(), v); }

struct Synth {
  bool vertex = false, r8 = false, s3 = false, twice = false, texld = false, swapped = false, other_x = false;
  bool no_parab = false, no_colour = false, writes_f = false;
};

// win32_30's pixel shader 1 in small: the depth read, the paraboloid's
// texldl and the two fades; `head` and `tail` the parts around the blend
// and the declaration a patch puts in.
Tokens Synthetic(const Synth& o, Tokens* head = nullptr, Tokens* middle = nullptr, Tokens* tail = nullptr) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  const Tokens ctab = Ctab({{"GBufferTextureSampler3", 3, 4},
                            {o.no_parab ? "OtherSampler" : "ParabSampler", 3, 5},
                            {"GBufferTextureSampler1", 3, 1},
                            {"GBufferTextureSampler2", 3, 2},
                            {"gDirectionalLight", 2, 17},
                            {o.no_colour ? "gOtherColour" : "gDirectionalColour", 2, 18},
                            {"gLightAmbient0", 2, 37},
                            {"gLightAmbient1", 2, 38},
                            {"dReflectionParams", 2, 72}});
  t.insert(t.end(), ctab.begin(), ctab.end());
  Add(t, {0x0200001F, 0x80000005, 0x90030000});  // dcl_texcoord0 v0.xy
  Add(t, {0x0200001F, 0x90000000, 0xA00F0804});  // dcl_2d s4
  Add(t, {0x0200001F, 0x90000000, 0xA00F0805});  // dcl_2d s5
  if (o.s3) Add(t, {0x0200001F, 0x90000000, 0xA00F0803});  // dcl_2d s3
  if (head) *head = t;
  const size_t mid = t.size();
  Add(t, {0x03000042, 0x800F0000, 0x90E40000, 0xA0E40804});  // texld r0, v0, s4
  if (o.r8) Add(t, {0x02000001, 0x800F0008, 0x80E40000});    // mov r8, r0
  if (o.twice) Add(t, {0x0300005F, 0x800F0002, 0x80E40005, 0xA0E40805});  // texldl r2, r5, s5
  Add(t, {o.texld ? 0x03000042u : 0x0300005Fu, 0x800F0003, 0x80E40005, 0xA0E40805});  // texldl r3, r5, s5
  if (middle) middle->assign(t.begin() + std::ptrdiff_t(mid), t.end());
  const size_t at = t.size();
  if (o.swapped)
    Add(t, {0x03000005, 0x800E0001, 0x80900003, 0x80AA0000});  // mul r1.yzw, r3.xxyz, r0.z
  else if (o.writes_f)
    Add(t, {0x03000005, 0x80070000, 0x80AA0000, 0x80900003});  // mul r0.xyz, r0.z, r3.xxyz
  else
    Add(t, {0x03000005, 0x800E0001, 0x80AA0000, 0x80900003});  // mul r1.yzw, r0.z, r3.xxyz
  if (o.writes_f)
    Add(t, {0x03000005, 0x800D0000, 0x80000000, 0x80E50000});  // mul r0.xzw, r0.x, r0.yyzw
  else if (o.other_x)
    Add(t, {0x03000005, 0x800D0000, 0x80000000, 0x80E50002});  // mul r0.xzw, r0.x, r2.yyzw
  else
    Add(t, {0x03000005, 0x800D0000, 0x80000000, 0x80E50001});  // mul r0.xzw, r0.x, r1.yyzw
  Add(t, {0x02000001, 0x800F0800, 0x80E40000});  // mov oC0, r0
  Add(t, {0x0000FFFF});
  if (tail) tail->assign(t.begin() + std::ptrdiff_t(at), t.end());
  return t;
}

void SyntheticChecks() {
  Tokens head, middle, tail;
  Tokens t = Synthetic({}, &head, &middle, &tail);
  Tokens want = head;
  Add(want, {0x0200001F, 0x90000000, 0xA00F0803});  // dcl_2d s3
  want.insert(want.end(), middle.begin(), middle.end());
  Add(want, {0x03000042, 0x800F0008, 0x90E40000, 0xA0E40803});              // texld r8, v0, s3
  Add(want, {0x04000012, 0x80070003, 0x80FF0008, 0x80E40008, 0x80E40003});  // lrp r3.xyz, r8.w, r8, r3
  Add(want, {0x0300000B, 0x80040000, 0x80AA0000, 0x80FF0008});              // max r0.z, r0.z, r8.w
  Add(want, {0x0300000B, 0x80010000, 0x80000000, 0x80FF0008});              // max r0.x, r0.x, r8.w
  want.insert(want.end(), tail.begin(), tail.end());
  const bool patched = gr::PatchReflectionLookup(t);
  Check(patched && t == want,
        "synthetic: dcl_2d s3, then after the texldl the read of s3, the blend and the fades' max");
  Check(gr::IsPatchedReflectionShader(t) && !gr::IsPatchedReflectionShader(Synthetic({})),
        "synthetic: the consumer takes the patched shader, not the original");
  Tokens again = t;
  Check(!gr::PatchReflectionLookup(again) && again == t, "synthetic: a patched shader not patched again");
  struct Bad {
    Synth s;
    const char* what;
  };
  Synth vs, r8, s3, twice, texld, swapped, other_x, no_parab, no_colour, writes_f;
  vs.vertex = true;
  r8.r8 = true;
  s3.s3 = true;
  twice.twice = true;
  texld.texld = true;
  swapped.swapped = true;
  other_x.other_x = true;
  no_parab.no_parab = true;
  no_colour.no_colour = true;
  writes_f.writes_f = true;
  const Bad kBad[] = {{vs, "a vertex shader"},
                      {r8, "r8 used"},
                      {s3, "s3 declared"},
                      {twice, "the paraboloid read twice"},
                      {texld, "the paraboloid read by texld"},
                      {swapped, "the first mul's operands swapped"},
                      {other_x, "the second mul not on the first's result"},
                      {no_parab, "no ParabSampler"},
                      {no_colour, "no gDirectionalColour"},
                      {writes_f, "the first mul writing the distance fade"}};
  bool left = true;
  for (const Bad& b : kBad) {
    const Tokens orig = Synthetic(b.s);
    Tokens x = orig;
    if (gr::PatchReflectionLookup(x) || x != orig) {
      std::printf("  patched: %s\n", b.what);
      left = false;
    }
  }
  Check(left, "synthetic: left alone with r8 or s3 used, the paraboloid read twice or by texld, other muls, the "
              "first one writing f, a CTAB without ParabSampler or another name the consumer reads, a vertex shader");
  // The consumer takes exactly the patched shader, and none whose CTAB lacks
  // what it reads.
  recomp::gfx::RayReflection r;
  Tokens nc = Synthetic(no_colour);
  Check(gr::ReflectionConsumer(t, &r) && r.output_sampler == 3 && r.depth_sampler == 4 && r.light_register == 17 &&
            !gr::ReflectionConsumer(Synthetic({}), &r) && !gr::ReflectionConsumer(nc, &r),
        "synthetic: the consumer's answer for the patched shader only");
}

// ---- the game's deferred_lighting.fxc

struct Fragment {
  bool pixel;
  int index;
  Tokens tokens;
};

// tools/fxc_shaders.py's reading of an rgxa effect.
bool Fragments(const std::vector<uint8_t>& d, std::vector<Fragment>* out) {
  if (d.size() < 5 || std::memcmp(d.data(), "rgxa", 4) != 0) return false;
  size_t p = 4;
  for (int stage = 0; stage < 2; stage++) {
    if (p >= d.size()) return false;
    const int n = d[p++];
    for (int k = 0; k < n; k++) {
      if (p >= d.size()) return false;
      const int vars = d[p++];
      for (int v = 0; v < vars; v++) {
        p += 4;
        if (p >= d.size()) return false;
        p += 1 + d[p];
      }
      if (p + 4 > d.size()) return false;
      const size_t size = size_t(d[p]) | size_t(d[p + 1]) << 8;
      p += 4;
      if (p + size > d.size()) return false;
      if (size) {
        Fragment f{stage == 1, k, Tokens(size / 4)};
        std::memcpy(f.tokens.data(), d.data() + p, size / 4 * 4);
        out->push_back(std::move(f));
      }
      p += size;
    }
  }
  return true;
}

// The instructions' first tokens.
std::vector<size_t> Starts(const Tokens& t) {
  std::vector<size_t> at;
  gtaiv_sun_shadow::detail::Instructions(t, &at);
  return at;
}

uint32_t RegType(uint32_t r) { return gtaiv_sun_shadow::detail::RegType(r); }

// Declarations of s3, reads of s3, texldl from sampler `parab`.
void Counts(const Tokens& t, int parab, int* dcls, int* reads, int* texldl) {
  *dcls = *reads = *texldl = 0;
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF;
    const uint32_t n = (t[i] >> 24) & 0xF;
    if (op == 0x1F && n == 2 && t[i + 1] == 0x90000000u && RegType(t[i + 2]) == 10 && (t[i + 2] & 0x7FF) == 3)
      (*dcls)++;
    if ((op == 0x42 || op == 0x5F) && n == 3 && RegType(t[i + 3]) == 10) {
      if ((t[i + 3] & 0x7FF) == 3) (*reads)++;
      if (op == 0x5F && int(t[i + 3] & 0x7FF) == parab) (*texldl)++;
    }
  }
}

// Whether, after the paraboloid's texldl, a mul by a def's constant 10
// (replicated) scales the term: the output's scale (gr::kTermScale).
bool TimesTen(const Tokens& t, int parab) {
  std::vector<uint32_t> tens;  // register * 4 + lane of the def constants equal to 10
  bool after = false;
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF;
    if (op == 0x51) {
      for (int k = 0; k < 4; k++) {
        float v;
        std::memcpy(&v, &t[i + 2 + k], 4);
        if (v == gr::kTermScale) tens.push_back((t[i + 1] & 0x7FF) * 4 + uint32_t(k));
      }
      continue;
    }
    if (op == 0x5F && RegType(t[i + 3]) == 10 && int(t[i + 3] & 0x7FF) == parab) after = true;
    if (!after || op != 0x05) continue;
    for (int s = 2; s <= 3; s++) {
      const uint32_t r = t[i + size_t(s)];
      const uint32_t sw = (r >> 16) & 0xFF, lane = sw & 3;
      if (RegType(r) != 2 || sw != lane * 0x55) continue;
      for (uint32_t c : tens)
        if (c == (r & 0x7FF) * 4 + lane) return true;
    }
  }
  return false;
}

void GameChecks(const char* game) {
  int folders = 0;
  for (const char* folder :
       {"win32_30", "win32_30_nv6", "win32_30_nv7", "win32_30_nv8", "win32_30_atidx10", "win32_30_low_ati"}) {
    const std::string path = std::string(game) + "/common/shaders/" + folder + "/deferred_lighting.fxc";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      Check(false, (path + ": there").c_str());
      continue;
    }
    folders++;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Fragment> frags;
    if (!Fragments(data, &frags)) {
      Check(false, (path + ": an rgxa effect").c_str());
      continue;
    }
    std::set<int> patched;
    bool ok = true, both = true;
    for (const Fragment& f : frags) {
      Tokens t = f.tokens;
      if (!gr::PatchReflectionLookup(t)) {
        if (gr::IsPatchedReflectionShader(f.tokens)) ok = false;
        continue;
      }
      if (!f.pixel) {
        ok = false;
        continue;
      }
      patched.insert(f.index);
      const int parab = gtaiv_shader::CtabRegister(f.tokens, "ParabSampler", gtaiv_shader::kSamplerSet);
      int dcls, reads, texldl, dcls0, reads0, texldl0;
      Counts(f.tokens, parab, &dcls0, &reads0, &texldl0);
      Counts(t, parab, &dcls, &reads, &texldl);
      Tokens again = t;
      // What the consumer answers (reflection_tokens.h's ReflectionConsumer,
      // which shadows_lights.cpp returns).
      const bool wet = f.index == 2 || f.index == 4;
      recomp::gfx::RayReflection r;
      const bool ctab =
          gr::ReflectionConsumer(t, &r) && !gr::ReflectionConsumer(f.tokens, &r) && gr::ReflectionConsumer(t, &r) &&
          r.output_sampler == 3 && r.depth_sampler == 4 && r.normal_sampler == 1 && r.surface_sampler == 2 &&
          r.spec_channel == 0 && r.gloss_channel == 1 && r.exponent_scale == 512 && r.blur_register == 72 &&
          r.blur_component == 0 && r.light_register == 17 && !r.toward_light && r.colour_register == 18 &&
          r.ambient_registers[0] == 37 && r.ambient_registers[1] == 38 && r.up[2] == 1 && r.diffuse_wrap == 0.25f &&
          r.output_scale == 0.1f && r.max_distance == 100 &&
          (wet ? r.wet_slope == 4 && r.wet_gain == 8 && r.wet_max_exponent == 256 && r.wet_register == 72 &&
                     r.wet_component == 1 && r.stencil_sampler == 5
               : r.wet_slope == 0 && r.stencil_sampler == 16);
      const bool shape = dcls0 == 0 && reads0 == 0 && texldl0 == 1 && dcls == 1 && reads == 1 && texldl == 1 &&
                         t.size() == f.tokens.size() + 20 && gr::IsPatchedReflectionShader(t) &&
                         !gr::PatchReflectionLookup(again) && TimesTen(f.tokens, parab);
      recomp::hlsl::Program prog;
      std::string err;
      const bool translated = recomp::hlsl::TranslateBytecode(t.data(), t.size(), &prog, &err);
      if (!translated) std::printf("  %s ps%d: %s\n", folder, f.index, err.c_str());
      if (!shape || !ctab)
        std::printf("  %s ps%d: s3 declared %d read %d, texldl %d, %zu tokens -> %zu, ctab %d, times ten %d\n",
                    folder, f.index, dcls, reads, texldl, f.tokens.size(), t.size(), ctab,
                    TimesTen(f.tokens, parab));
      ok = ok && shape && ctab && translated && prog.stage == recomp::hlsl::Stage::Pixel;
      // With the sun shadow's patch (pixel shaders 1 and 2), in either order.
      Tokens a = f.tokens, b = f.tokens;
      const bool sa = ss::PatchSunShadowLookup(a), ra = gr::PatchReflectionLookup(a);
      const bool rb = gr::PatchReflectionLookup(b), sb = ss::PatchSunShadowLookup(b);
      const bool sun = f.index == 1 || f.index == 2;
      if (sa != sun || sb != sun || !ra || !rb || a != b) {
        both = false;
        continue;
      }
      if (!sun) continue;
      recomp::hlsl::Program pa;
      const bool ta = recomp::hlsl::TranslateBytecode(a.data(), a.size(), &pa, &err);
      if (!ta) std::printf("  %s ps%d with both: %s\n", folder, f.index, err.c_str());
      both = both && ta && ss::IsPatchedSunShader(a) && gr::IsPatchedReflectionShader(a);
      std::printf("  %s ps%d: %zu tokens -> %zu (with the sun shadow's patch %zu)\n", folder, f.index,
                  f.tokens.size(), t.size(), a.size());
    }
    Check(ok && patched == std::set<int>{1, 2, 3, 4},
          (std::string(folder) + ": pixel shaders 1-4 only patched, each once (dcl_2d s3, one read of s3, the "
                                 "texldl kept), the consumer's answer, the term times 10, translated")
              .c_str());
    Check(both, (std::string(folder) + ": pixel shaders 1 and 2 with the sun shadow's patch too, in either order, "
                                       "the same; 3 and 4 without it")
                    .c_str());
  }
  Check(folders == 6, "the game's deferred_lighting.fxc in all six shader folders");
}

}  // namespace

int main() {
  SyntheticChecks();
  if (const char* game = std::getenv("GTAIV_GAME_DIR"); game && *game)
    GameChecks(game);
  else
    std::printf("skipped the game's shaders: GTAIV_GAME_DIR not set\n");
  if (failures) return 1;
  std::puts("reflection patch: ok");
  return 0;
}
