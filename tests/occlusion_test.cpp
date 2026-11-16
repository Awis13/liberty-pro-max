// The sun pass's ambient times the traced occlusion (src/occlusion_tokens.h).
// Always: a synthetic ps_3_0 shaped as the game's (patched as expected: the
// read and the mul after the ambient's mad, dcl_2d s7 after the other
// declarations; taken by the consumer's check, not patched twice; the
// consumer's answer for it only) and left alone where s7 is used, the
// anchor is there twice, the mad is not the ambient's, the depth is read
// from a temp, the CTAB lacks a name, or it is a vertex shader. With
// GTAIV_GAME_DIR set (the install): the game's own deferred_lighting.fxc of
// each of the six shader folders (all must be there): pixel shaders 1-4
// patched and no other, each once (11 tokens more), the consumer's answer
// for them (s7; s4, s1) and not for the originals, the result translates
// (hlsl::TranslateBytecode); and with the sun shadow's and the reflections'
// patches applied before it as shadows_lights.cpp does, still patched,
// each patch's own check still true, translated; skipped without it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "occlusion_tokens.h"
#include "reflection_tokens.h"
#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace go = gtaiv_occlusion;
namespace gr = gtaiv_reflection;
namespace ss = gtaiv_sun_shadow;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

// ---- a synthetic shader

// A CTAB comment naming the registers ({name, set, register}), as
// gtaiv_shader::CtabRegister reads it.
void Ctab(Tokens* out, const std::vector<std::pair<const char*, std::pair<int, int>>>& regs) {
  const size_t n = regs.size();
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
  for (size_t i = 0; i < n; i++) {
    const uint32_t name = str(regs[i].first);
    put32(28 + i * 20, name);
    put16(28 + i * 20 + 4, uint16_t(regs[i].second.first));
    put16(28 + i * 20 + 6, uint16_t(regs[i].second.second));
    put16(28 + i * 20 + 8, 1);
  }
  put32(4, str("test"));
  put32(24, str("ps_3_0"));
  while (t.size() % 4) t.push_back(0);
  out->push_back(0xFFFEu | uint32_t(t.size() / 4 + 1) << 16);
  out->push_back(0x42415443u);  // 'CTAB'
  for (size_t k = 0; k < t.size(); k += 4) {
    uint32_t v;
    std::memcpy(&v, &t[k], 4);
    out->push_back(v);
  }
}

struct Synth {
  bool use_s7 = false, twice = false, other_mad = false, depth_temp = false, no_normals = false, vertex = false;
};

Tokens Synthetic(const Synth& o) {
  using gtaiv_shader::kSamplerSet, gtaiv_shader::kFloatSet;
  Tokens t{o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  std::vector<std::pair<const char*, std::pair<int, int>>> regs = {{"gLightAmbient0", {kFloatSet, 37}},
                                                                   {"gLightAmbient1", {kFloatSet, 38}},
                                                                   {"GBufferTextureSampler3", {kSamplerSet, 4}}};
  if (!o.no_normals) regs.push_back({"GBufferTextureSampler1", {kSamplerSet, 1}});
  Ctab(&t, regs);
  const uint32_t dcls[] = {
      0x0200001Fu, 0x80000005u, 0x90030000u,  // dcl_texcoord v0.xy
      0x0200001Fu, 0x90000000u, 0xA00F0801u,  // dcl_2d s1
      0x0200001Fu, 0x90000000u, 0xA00F0804u,  // dcl_2d s4
  };
  t.insert(t.end(), std::begin(dcls), std::end(dcls));
  if (o.use_s7) t.insert(t.end(), {0x0200001Fu, 0x90000000u, 0xA00F0807u});
  if (o.depth_temp) t.insert(t.end(), {0x02000001u, 0x80030003u, 0x90E40000u});  // mov r3.xy, v0
  t.insert(t.end(),
           {0x03000042u, 0x800F0000u, o.depth_temp ? 0x80E40003u : 0x90E40000u, 0xA0E40804u});  // texld r0, v0, s4
  t.insert(t.end(), {0x03000042u, 0x800F0001u, 0x90E40000u, 0xA0E40801u});                      // texld r1, v0, s1
  for (int k = 0; k < (o.twice ? 2 : 1); k++) {
    t.insert(t.end(), {0x02000001u, 0x80070002u, 0xA0E40026u});  // mov r2.xyz, c38
    // mad r1.yzw, r2.xxyz, r1.y, c37.xxyz (or c36: not the ambient)
    t.insert(t.end(), {0x04000004u, 0x800E0001u, 0x80900002u, 0x80550001u, o.other_mad ? 0xA0900024u : 0xA0900025u});
  }
  t.insert(t.end(), {0x03000005u, 0x800E0001u, 0x80AA0000u, 0x80E40001u});  // mul r1.yzw, r0.z, r1
  t.insert(t.end(), {0x02000001u, 0x800F0800u, 0x80E40001u});               // mov oC0, r1
  if (o.use_s7) t.insert(t.end(), {0x03000042u, 0x800F0004u, 0x90E40000u, 0xA0E40807u});
  t.push_back(0x0000FFFFu);
  return t;
}

void SyntheticChecks() {
  Tokens t = Synthetic({});
  const Tokens orig = t;
  const bool patched = go::PatchOcclusionLookup(t);
  Check(patched && t.size() == orig.size() + 11, "synthetic: patched, 11 tokens more");
  {
    Tokens tb = orig;
    const bool bp = go::PatchOcclusionLookup(tb, true);
    bool mad = false;
    for (size_t i = 0; i + 4 < tb.size(); i++)
      if (tb[i] == 0x04000004u && (tb[i + 3] & 0x00FF0000u) == 0x00FF0000u) mad = true;
    Check(bp && tb.size() == orig.size() + 12 && go::IsPatchedOcclusionShader(tb) && mad,
          "synthetic, bounce: the ambient times s7's alpha plus its rgb (a mad), 12 tokens more, taken by the check");
  }
  Tokens again = t;
  Check(go::IsPatchedOcclusionShader(t) && !go::IsPatchedOcclusionShader(orig) && !go::PatchOcclusionLookup(again) &&
            again == t,
        "synthetic: taken by the check, the original not; not patched twice");
  recomp::gfx::RayOcclusion o;
  Check(go::OcclusionConsumer(t, &o) && o.occlusion_sampler == 7 && o.depth_sampler == 4 && o.normal_sampler == 1 &&
            !go::OcclusionConsumer(orig, &o),
        "synthetic: the consumer's answer (s7; s4, s1) for the patched shader only");
  recomp::hlsl::Program prog;
  std::string err;
  Check(recomp::hlsl::TranslateBytecode(t.data(), t.size(), &prog, &err), "synthetic: the patched shader translates");
  for (auto [s, what] : std::initializer_list<std::pair<Synth, const char*>>{
           {{.use_s7 = true}, "s7 used"},
           {{.twice = true}, "the anchor twice"},
           {{.other_mad = true}, "the mad's third source not the ambient"},
           {{.depth_temp = true}, "the depth read from a temp"},
           {{.no_normals = true}, "the CTAB without the normals"},
           {{.vertex = true}, "a vertex shader"}}) {
    Tokens u = Synthetic(s);
    const Tokens u0 = u;
    Check(!go::PatchOcclusionLookup(u) && u == u0, (std::string("synthetic: left alone where ") + what).c_str());
  }
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
    bool ok = true, all = true;
    for (const Fragment& f : frags) {
      Tokens t = f.tokens;
      if (!go::PatchOcclusionLookup(t)) {
        if (go::IsPatchedOcclusionShader(f.tokens)) ok = false;
        continue;
      }
      if (!f.pixel) {
        ok = false;
        continue;
      }
      patched.insert(f.index);
      Tokens again = t;
      recomp::gfx::RayOcclusion o;
      const bool answer = go::OcclusionConsumer(t, &o) && o.occlusion_sampler == 7 && o.depth_sampler == 4 &&
                          o.normal_sampler == 1 && !go::OcclusionConsumer(f.tokens, &o);
      const bool shape = t.size() == f.tokens.size() + 11 && !go::PatchOcclusionLookup(again);
      recomp::hlsl::Program prog;
      std::string err;
      const bool translated = recomp::hlsl::TranslateBytecode(t.data(), t.size(), &prog, &err);
      if (!translated) std::printf("  %s ps%d: %s\n", folder, f.index, err.c_str());
      ok = ok && answer && shape && translated && prog.stage == recomp::hlsl::Stage::Pixel;
      // As shadows_lights.cpp applies them: the sun's shadow, the
      // reflections, then the occlusion.
      Tokens a = f.tokens;
      const bool sun = ss::PatchSunShadowLookup(a);
      const bool refl = gr::PatchReflectionLookup(a);
      const bool occ = go::PatchOcclusionLookup(a);
      recomp::hlsl::Program pa;
      const bool ta = occ && recomp::hlsl::TranslateBytecode(a.data(), a.size(), &pa, &err);
      if (occ && !ta) std::printf("  %s ps%d with all: %s\n", folder, f.index, err.c_str());
      const bool each = go::IsPatchedOcclusionShader(a) && (!sun || ss::IsPatchedSunShader(a)) &&
                        (!refl || gr::IsPatchedReflectionShader(a)) && go::OcclusionConsumer(a, &o);
      all = all && occ && ta && each && refl == true && sun == (f.index == 1 || f.index == 2);
      std::printf("  %s ps%d: %zu tokens -> %zu (with all three %zu)\n", folder, f.index, f.tokens.size(), t.size(),
                  a.size());
    }
    Check(ok && patched == std::set<int>{1, 2, 3, 4},
          (std::string(folder) + ": pixel shaders 1-4 only patched, each once, the consumer's answer, translated")
              .c_str());
    Check(all, (std::string(folder) + ": with the sun shadow's and the reflections' patches first, each still "
                                      "taken, translated")
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
  std::puts("occlusion patch: ok");
  return 0;
}
