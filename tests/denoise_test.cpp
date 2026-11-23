// The shaders that take part in MetalFX's denoising of the lit scene
// (src/denoise_tokens.h). Always: synthetic ps_3_0 tokens with a CTAB (a
// post-process shader naming HDRSampler: its register; a lighting shader
// naming the G-buffer's three colour samplers and gDirectionalLight: those
// registers and the game's constants; one lacking any of those, and a vertex
// shader naming HDRSampler, not taken). With GTAIV_GAME_DIR set (the
// install): in each of the six shader folders, rage_postfx.fxc's pixel
// shaders naming HDRSampler taken as post-process inputs at that register,
// the others (and the vertex shaders) not, every one still translating
// (hlsl::TranslateBytecode); deferred_lighting.fxc's sun pass shaders (the
// ones the reflections' patch takes) taken with their G-buffer and no other
// of its shaders; skipped without it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "denoise_tokens.h"
#include "reflection_tokens.h"
#include "runtime/hlsl/hlsl.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace gd = gtaiv_denoise;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

// A shader of version `version` whose CTAB names (name, set, register).
struct Entry {
  const char* name;
  uint16_t set, reg;
};
Tokens Synthetic(uint32_t version, const std::vector<Entry>& entries) {
  // The table: header (28 bytes), entries (20 each), names.
  std::vector<uint8_t> tab(28 + 20 * entries.size());
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&tab[at], &v, 4); };
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&tab[at], &v, 2); };
  put32(0, 28);
  put32(8, version);
  put32(12, uint32_t(entries.size()));
  put32(16, 28);
  for (size_t i = 0; i < entries.size(); i++) {
    const size_t at = 28 + 20 * i;
    put32(at, uint32_t(tab.size()));
    put16(at + 4, entries[i].set);
    put16(at + 6, entries[i].reg);
    put16(at + 8, 1);
    tab.insert(tab.end(), entries[i].name, entries[i].name + std::strlen(entries[i].name) + 1);
  }
  while (tab.size() % 4) tab.push_back(0);
  Tokens t{version};
  const uint32_t n = uint32_t(tab.size() / 4 + 1);
  t.push_back(0xFFFE | (n << 16));
  t.push_back(0x42415443);  // 'CTAB'
  for (size_t i = 0; i < tab.size(); i += 4) {
    uint32_t v;
    std::memcpy(&v, &tab[i], 4);
    t.push_back(v);
  }
  t.push_back(0x0000FFFF);
  return t;
}

void SyntheticChecks() {
  using gtaiv_shader::kFloatSet, gtaiv_shader::kSamplerSet;
  recomp::gfx::RayDenoise d;
  const Tokens post = Synthetic(0xFFFF0300, {{"AdapLumSampler", kSamplerSet, 4}, {"HDRSampler", kSamplerSet, 2}});
  Check(gd::DenoiseConsumer(post, &d) && d.post_input && d.input_sampler == 2,
        "synthetic: a post-process shader's HDRSampler (s2)");
  const Tokens light = Synthetic(0xFFFF0300, {{"GBufferTextureSampler0", kSamplerSet, 0},
                                              {"GBufferTextureSampler1", kSamplerSet, 1},
                                              {"GBufferTextureSampler2", kSamplerSet, 2},
                                              {"GBufferTextureSampler3", kSamplerSet, 4},
                                              {"gDirectionalLight", kFloatSet, 17}});
  d = {};
  Check(gd::DenoiseConsumer(light, &d) && !d.post_input && d.albedo_sampler == 0 && d.normal_sampler == 1 &&
            d.surface_sampler == 2 && d.spec_channel == 0 && d.gloss_channel == 1 &&
            d.exponent_scale == gd::kExponentScale && d.spec_scale == gd::kSpecScale &&
            d.fresnel_min == gd::kFresnelMin && d.fresnel_power == gd::kFresnelPower,
        "synthetic: the sun pass's G-buffer (s0, s1, s2) and the game's constants");
  const Tokens volume = Synthetic(0xFFFF0300, {{"GBufferTextureSampler0", kSamplerSet, 0},
                                               {"GBufferTextureSampler1", kSamplerSet, 1},
                                               {"GBufferTextureSampler2", kSamplerSet, 2},
                                               {"gDeferredLightPosition", kFloatSet, 66}});
  const Tokens vertex = Synthetic(0xFFFE0300, {{"HDRSampler", kSamplerSet, 0}});
  Check(!gd::DenoiseConsumer(volume, &d) && !gd::DenoiseConsumer(vertex, &d) &&
            !gd::DenoiseConsumer(Synthetic(0xFFFF0300, {}), &d),
        "synthetic: a light volume, a vertex shader and a shader naming nothing not taken");
}

// ---- the game's shaders

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

bool Load(const std::string& path, std::vector<Fragment>* frags) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Check(false, (path + ": there").c_str());
    return false;
  }
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!Fragments(data, frags)) {
    Check(false, (path + ": an rgxa effect").c_str());
    return false;
  }
  return true;
}

void GameChecks(const char* game) {
  int folders = 0;
  for (const char* folder :
       {"win32_30", "win32_30_nv6", "win32_30_nv7", "win32_30_nv8", "win32_30_atidx10", "win32_30_low_ati"}) {
    const std::string dir = std::string(game) + "/common/shaders/" + folder;
    std::vector<Fragment> post, light;
    if (!Load(dir + "/rage_postfx.fxc", &post) || !Load(dir + "/deferred_lighting.fxc", &light)) continue;
    folders++;
    // The post-processing: every pixel shader naming HDRSampler, at its
    // register; nothing else; all still translate.
    int named = 0, taken = 0, others = 0;
    bool right = true, translated = true;
    for (const Fragment& f : post) {
      recomp::gfx::RayDenoise d;
      const int hdr = gtaiv_shader::CtabRegister(f.tokens, "HDRSampler", gtaiv_shader::kSamplerSet);
      const bool took = gd::DenoiseConsumer(f.tokens, &d);
      if (f.pixel && hdr >= 0) {
        named++;
        if (took && d.post_input && d.input_sampler == uint32_t(hdr)) taken++;
      } else {
        others += took;
      }
      if (took && (!d.post_input || !f.pixel)) right = false;
      recomp::hlsl::Program p;
      std::string err;
      if (f.pixel && took && !recomp::hlsl::TranslateBytecode(f.tokens.data(), f.tokens.size(), &p, &err))
        translated = false;
    }
    char what[256];
    std::snprintf(what, sizeof what,
                  "%s rage_postfx: %d pixel shaders name HDRSampler, %d taken at its register, %d others taken; all "
                  "taken translate",
                  folder, named, taken, others);
    Check(named >= 20 && taken == named && others == 0 && right && translated, what);
    // The lighting: the sun pass's shaders (the reflections' patch's), with
    // their G-buffer; no other.
    std::set<int> guides, sun;
    bool regs = true;
    for (const Fragment& f : light) {
      recomp::gfx::RayDenoise d;
      if (!gd::DenoiseConsumer(f.tokens, &d)) continue;
      if (!f.pixel || d.post_input) {
        regs = false;
        continue;
      }
      guides.insert(f.index);
      using gtaiv_shader::CtabRegister, gtaiv_shader::kSamplerSet;
      regs = regs && int(d.albedo_sampler) == CtabRegister(f.tokens, "GBufferTextureSampler0", kSamplerSet) &&
             int(d.normal_sampler) == CtabRegister(f.tokens, "GBufferTextureSampler1", kSamplerSet) &&
             int(d.surface_sampler) == CtabRegister(f.tokens, "GBufferTextureSampler2", kSamplerSet);
    }
    for (const Fragment& f : light) {
      Tokens t = f.tokens;
      if (f.pixel && gtaiv_reflection::PatchReflectionLookup(t)) sun.insert(f.index);
    }
    std::snprintf(what, sizeof what,
                  "%s deferred_lighting: %zu shaders taken with their G-buffer, the same as the reflections' patch's "
                  "sun pass (%zu)",
                  folder, guides.size(), sun.size());
    Check(regs && !guides.empty() && guides == sun, what);
  }
  Check(folders == 6, "the game's rage_postfx.fxc and deferred_lighting.fxc in all six shader folders");
}

}  // namespace

int main() {
  SyntheticChecks();
  if (const char* game = std::getenv("GTAIV_GAME_DIR"); game && *game)
    GameChecks(game);
  else
    std::printf("skipped the game's shaders: GTAIV_GAME_DIR not set\n");
  if (failures) return 1;
  std::puts("denoise shaders: ok");
  return 0;
}
