// The local lights the renderer lights itself (src/light_volume_tokens.h):
// the pixel classes as the light volumes' shaders test them (interior m >=
// 8, exterior m < 8, class one m of 1 or 9, m the value less 128 above it)
// and the renderer's class sets made from that; the models' numbers. With
// GTAIV_GAME_DIR set (the install), in each of the six shader folders:
// deferred_lighting.fxc's pixel shaders that name the G-buffer taken (the
// sun pass 1-4, the volumes 6-12 and 14-17), the volumes (those naming
// gDeferredLightPosition) marked, their samplers as their CTABs name them;
// no shader of another effect marked a volume.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "light_volume_tokens.h"

namespace {

namespace lv = gtaiv_light_volume;
using Tokens = std::vector<uint32_t>;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

void SyntheticChecks() {
  bool interior = true, exterior = true, one = true;
  for (uint32_t v = 0; v < 256; v++) {
    const uint32_t m = v & 127;
    interior = interior && lv::InClass(lv::kClassInterior, v) == (m >= 8);
    exterior = exterior && lv::InClass(lv::kClassExterior, v) == (m < 8);
    one = one && lv::InClass(lv::kClassOne, v) == (m == 1 || m == 9);
  }
  Check(interior, "classes: interior where the value less 128 above it is 8 or more");
  Check(exterior, "classes: exterior where it is below 8");
  Check(one, "classes: class one where it is 1 or 9");
  const recomp::gfx::RayManyLights c = lv::ManyLights();
  bool sets = true;
  for (uint32_t k = 1; k <= 3; k++)
    for (uint32_t v = 0; v < 256; v++)
      sets = sets && (((c.classes[k - 1][v >> 5] >> (v & 31)) & 1u) != 0) == lv::InClass(k, v);
  Check(sets, "classes: the renderer's sets hold exactly those values");
  const recomp::gfx::RayLightModel& l = c.models[lv::kModelLight];
  const recomp::gfx::RayLightModel& f = c.models[lv::kModelFiller];
  Check(l.falloff_power == 4 && l.falloff_bias == 0.1f && l.specular && l.spec_scale == 2 && l.exponent_scale == 512 &&
            l.normal_low_bits && l.factor_classes == ((1u << lv::kClassAll) | (1u << lv::kClassExterior)),
        "model 0: a^4 - 0.1, specular 2 s.x at 512 s.y^2, the factor on every pixel's and exterior lights, the "
        "normal's low bits");
  Check(f.falloff_power == 2 && f.falloff_bias == 1.0f / 3.0f && f.falloff_scale == 1.5f && f.wrap == 0.2f &&
            f.wrap_scale == 1.25f && !f.specular && !f.normal_low_bits && f.factor_classes == 0,
        "model 1: a^2 - 1/3 times 1.5, diffuse wrapped by 0.2, no specular, no factor");
  Check(c.lamp_gap == gtaiv_light_shadow::kLampGap, "the lamp gap the shadowed lights' rays have");
  Check(c.mode == recomp::gfx::RayManyLights::Mode::Only, "by default every light traced in place of the volumes");
  // Not a pixel shader, or no G-buffer: not taken.
  recomp::gfx::RayLightVolume v;
  Check(!lv::LightVolumeConsumer(Tokens{0xFFFE0300u, 0x0000FFFFu}, &v) && !lv::LightVolumeConsumer(Tokens{}, &v),
        "a vertex shader, or none: not taken");
}

// ---- the game's shaders (tools/fxc_shaders.py's reading of an rgxa effect)

struct Fragment {
  bool pixel;
  int index;
  Tokens tokens;
};

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

std::vector<uint8_t> Read(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void GameChecks(const char* game) {
  int folders = 0;
  for (const char* folder :
       {"win32_30", "win32_30_nv6", "win32_30_nv7", "win32_30_nv8", "win32_30_atidx10", "win32_30_low_ati"}) {
    const std::filesystem::path dir = std::filesystem::path(game) / "common" / "shaders" / folder;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
      Check(false, (dir.string() + ": there").c_str());
      continue;
    }
    folders++;
    std::set<int> taken, volumes;
    std::map<int, recomp::gfx::RayLightVolume> got;
    int others = 0, other_volumes = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".fxc") continue;
      std::vector<Fragment> frags;
      if (!Fragments(Read(e.path()), &frags)) continue;
      const bool lighting = e.path().stem() == "deferred_lighting";
      for (const Fragment& f : frags) {
        recomp::gfx::RayLightVolume v;
        if (!lv::LightVolumeConsumer(f.tokens, &v)) continue;
        if (!lighting) {
          others++;
          other_volumes += v.volume;
          continue;
        }
        taken.insert(f.index);
        if (v.volume) volumes.insert(f.index);
        got[f.index] = v;
      }
    }
    if (others) std::printf("  %s: %d shaders of other effects name the G-buffer\n", folder, others);
    const std::set<int> want_taken = {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17};
    const std::set<int> want_volumes = {6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17};
    auto is = [&](int k, uint32_t d, uint32_t a, uint32_t n, uint32_t s, uint32_t c) {
      const auto it = got.find(k);
      return it != got.end() && it->second.depth_sampler == d && it->second.albedo_sampler == a &&
             it->second.normal_sampler == n && it->second.surface_sampler == s && it->second.class_sampler == c;
    };
    const bool samplers = is(6, 4, 0, 1, 2, 16) && is(7, 4, 0, 1, 2, 5) && is(10, 2, 0, 1, 16, 16) &&
                          is(11, 2, 0, 1, 16, 4) && is(16, 5, 1, 2, 4, 16);
    if (taken != want_taken || volumes != want_volumes || !samplers) {
      std::printf("  %s: taken", folder);
      for (int k : taken) std::printf(" %d%s", k, volumes.count(k) ? "v" : "");
      std::printf("\n");
    }
    Check(taken == want_taken && volumes == want_volumes && samplers && other_volumes == 0,
          (std::string(folder) + ": deferred_lighting's sun pass (1-4) and light volumes (6-12, 14-17) taken, the "
                                 "volumes marked, their samplers by their CTABs; no volume in another effect")
              .c_str());
  }
  Check(folders == 6, "the game's shader folders, all six");
}

}  // namespace

int main() {
  SyntheticChecks();
  if (const char* game = std::getenv("GTAIV_GAME_DIR"); game && *game)
    GameChecks(game);
  else
    std::printf("skipped the game's shaders: GTAIV_GAME_DIR not set\n");
  if (failures) return 1;
  std::puts("light volumes: ok");
  return 0;
}
