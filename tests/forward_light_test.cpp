// The forward shaders' local lights shadowed by rays
// (src/forward_light_tokens.h). Always: a synthetic ps_3_0 in the vanilla
// shape (patched: the point gathered, a texldd of s15 a lane, the weights
// times them before the colours' dp4s, translated to four calls of the
// shadow toward a point) and refused (a vertex shader, the position
// negated, flow control inside, another sampler at s15). With
// GTAIV_GAME_DIR set (the install), the six shader folders, parsed as
// tools/fxc_shaders.py does: every pixel shader naming gLightPosX, after
// the sun's and the forward shadow's patches (the hook's order): patched,
// taken by the consumer (s15, the lamp's gap), translated with four calls
// a set and no s15; the sun's forward read still taken where it was; the
// vehicle glass's reflections' patch still taken after it. Skipped
// without it.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "forward_light_tokens.h"
#include "forward_shadow_tokens.h"
#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"
#include "vehglass_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace fl = gtaiv_forward_light;
namespace fs = gtaiv_forward_shadow;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

struct Entry {
  const char* name;
  uint16_t set, reg, count;
};
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
  size_t k = 0;
  for (const Entry& e : entries) {
    const uint32_t name = str(e.name);
    put32(28 + k * 20, name);
    put16(28 + k * 20 + 4, e.set);
    put16(28 + k * 20 + 6, e.reg);
    put16(28 + k * 20 + 8, e.count);
    k++;
  }
  put32(24, str("ps_3_0"));
  put32(4, str("test"));
  while (t.size() % 4) t.push_back(0);
  Tokens out;
  out.push_back(0xFFFEu | uint32_t(t.size() / 4 + 1) << 16);
  out.push_back(0x42415443u);  // 'CTAB'
  for (size_t i = 0; i < t.size(); i += 4) {
    uint32_t v;
    std::memcpy(&v, &t[i], 4);
    out.push_back(v);
  }
  return out;
}

void Add(Tokens& t, std::initializer_list<uint32_t> v) { t.insert(t.end(), v); }

size_t Count(const std::string& s, const char* what) {
  size_t n = 0;
  for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) n++;
  return n;
}

size_t Rays(const Tokens& t) {
  std::vector<fl::detail::Ins> is;
  if (!fl::detail::Parse(t, &is)) return 0;
  size_t n = 0;
  for (const auto& in : is) n += fl::detail::IsLightRay(t, in) ? 1 : 0;
  return n;
}

// Translated as the device does with the consumer's answer: the calls of
// the shadow toward a point, s15 not declared.
bool Translates(const Tokens& t, size_t* calls) {
  recomp::gfx::RayForwardShadow s;
  if (!fl::ForwardLightsConsumer(t, &s) || s.sampler != fl::kSampler || !(s.lamp_gap > 0)) return false;
  recomp::hlsl::TranslateOptions o;
  o.ray_shadow.sampler = int(s.sampler);
  o.ray_shadow.light_register = s.light_register;
  recomp::hlsl::Program p;
  std::string err;
  if (!recomp::hlsl::TranslateBytecode(t.data(), t.size(), &p, &err, o)) {
    std::printf("  translation: %s\n", err.c_str());
    return false;
  }
  *calls = Count(p.glsl, "r_rt_shadow_to(gl_FragCoord, ");
  return p.stage == recomp::hlsl::Stage::Pixel && p.ray_shadow_sampler == int(s.sampler) &&
         p.glsl.find("r_s15") == std::string::npos;
}

struct Synth {
  bool vertex = false, negated = false, flow_inside = false, other_sampler = false;
};

// ps_3_0 in the vanilla shape: v0 the point, gLightPosX/Y/Z c19-c21,
// gLightColR/G/B c29-c31; the weights r8 = the squared distances.
Tokens Synthetic(const Synth& o) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  const Tokens ctab = Ctab({{o.other_sampler ? "OtherSampler" : "gShadowZSamplerDir", 3, 15, 1},
                            {"gLightPosX", 2, 19, 1},
                            {"gLightPosY", 2, 20, 1},
                            {"gLightPosZ", 2, 21, 1},
                            {"gLightColR", 2, 29, 1},
                            {"gLightColG", 2, 30, 1},
                            {"gLightColB", 2, 31, 1}});
  t.insert(t.end(), ctab.begin(), ctab.end());
  Add(t, {0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000});  // def c0, 1, 0, 0, 0
  Add(t, {0x0200001F, 0x80000005, 0x90070000});  // dcl_texcoord0 v0.xyz
  if (o.other_sampler) Add(t, {0x0200001F, 0x90000000, 0xA00F080F});  // dcl_2d s15
  if (o.negated)
    Add(t, {0x03000002, 0x800F0005, 0xA1E40013, 0x90000000});  // add r5, -c19, v0.x
  else
    Add(t, {0x03000002, 0x800F0005, 0xA0E40013, 0x91000000});  // add r5, c19, -v0.x
  Add(t, {0x03000002, 0x800F0006, 0xA0E40014, 0x91550000});  // add r6, c20, -v0.y
  Add(t, {0x03000002, 0x800F0007, 0xA0E40015, 0x91AA0000});  // add r7, c21, -v0.z
  if (o.flow_inside) Add(t, {0x01000026, 0xF0E40000});  // rep i0
  Add(t, {0x03000005, 0x800F0008, 0x80E40005, 0x80E40005});  // mul r8, r5, r5
  Add(t, {0x04000004, 0x800F0008, 0x80E40006, 0x80E40006, 0x80E40008});  // mad r8, r6, r6, r8
  Add(t, {0x04000004, 0x800F0008, 0x80E40007, 0x80E40007, 0x80E40008});  // mad r8, r7, r7, r8
  if (o.flow_inside) Add(t, {0x00000027});  // endrep
  Add(t, {0x03000009, 0x80010001, 0xA0E4001D, 0x80E40008});  // dp4 r1.x, c29, r8
  Add(t, {0x03000009, 0x80020001, 0xA0E4001E, 0x80E40008});  // dp4 r1.y, c30, r8
  Add(t, {0x03000009, 0x80040001, 0xA0E4001F, 0x80E40008});  // dp4 r1.z, c31, r8
  Add(t, {0x02000001, 0x80080001, 0xA0000000});  // mov r1.w, c0.x
  Add(t, {0x02000001, 0x800F0800, 0x80E40001});  // mov oC0, r1
  t.push_back(0x0000FFFF);
  return t;
}

void SyntheticChecks() {
  const Tokens a = Synthetic({});
  Tokens p = a;
  size_t calls = 0;
  const bool ok = fl::PatchForwardLights(p) && Rays(p) == 4 && fl::HasForwardLightRays(p) &&
                  !fl::HasForwardLightRays(a) && Translates(p, &calls) && calls == 4;
  Check(ok, "synthetic: patched, four rays, taken by the consumer, translated to four calls");
  recomp::gfx::RayForwardShadow s;
  Check(!fl::ForwardLightsConsumer(a, &s), "synthetic: the vanilla shader is not the consumer's");
  bool left = true;
  for (const Synth& o : {Synth{true, false, false, false}, Synth{false, true, false, false},
                         Synth{false, false, true, false}, Synth{false, false, false, true}}) {
    const Tokens b = Synthetic(o);
    Tokens x = b;
    if (fl::PatchForwardLights(x) || x != b) left = false;
  }
  Check(left, "synthetic: left alone as a vertex shader, with the position negated, with flow control in the "
              "block, with another sampler at s15");
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
  if (!in) return false;
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return Fragments(data, frags);
}

void GameChecks(const char* game) {
  int folders = 0;
  size_t named = 0, patched = 0, refused = 0, untranslated = 0, wrong_calls = 0, sun_lost = 0, glass = 0,
         glass_lost = 0, rays = 0;
  for (const char* folder :
       {"win32_30", "win32_30_nv6", "win32_30_nv7", "win32_30_nv8", "win32_30_atidx10", "win32_30_low_ati"}) {
    const std::string dir = std::string(game) + "/common/shaders/" + folder;
    std::vector<std::string> effects;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
      if (e.path().extension() == ".fxc") effects.push_back(e.path().filename().string());
    std::sort(effects.begin(), effects.end());
    bool any = false;
    for (const std::string& name : effects) {
      std::vector<Fragment> frags;
      if (!Load(dir + "/" + name, &frags)) continue;
      any = true;
      for (const Fragment& f : frags) {
        if (!f.pixel || fl::detail::CtabFloat(f.tokens, "gLightPosX") < 0) continue;
        named++;
        // The hook's order (shadows_lights.cpp).
        Tokens t = f.tokens;
        gtaiv_sun_shadow::PatchSunShadowLookup(t);
        fs::PatchForwardShadowLookup(t);
        const bool sun = fs::IsPatchedForwardShadow(t);
        Tokens before = t;
        const bool glass_before = gtaiv_vehglass::PatchVehglassLookup(before);
        if (!fl::PatchForwardLights(t)) {
          refused++;
          if (refused <= 12) {
            Tokens x = t;
            int why = 0;
            fl::detail::PatchSet(x, fl::detail::kSets[0], &why);
            std::printf("  refused (%d): %s/%s ps%d\n", why, folder, name.c_str(), f.index);
          }
          continue;
        }
        patched++;
        const size_t n = Rays(t);
        rays += n;
        size_t calls = 0;
        if (!Translates(t, &calls)) {
          untranslated++;
          if (untranslated <= 12) std::printf("  untranslated: %s/%s ps%d\n", folder, name.c_str(), f.index);
        } else if (calls != n || (n != 4 && n != 8)) {
          wrong_calls++;
        }
        if (sun && !fs::IsPatchedForwardShadow(t)) sun_lost++;
        if (glass_before) {
          glass++;
          Tokens after = t;
          if (!gtaiv_vehglass::PatchVehglassLookup(after)) glass_lost++;
        }
      }
    }
    if (any) folders++;
  }
  std::printf("  %d folders: %zu pixel shaders name the lights, %zu patched (%zu rays), %zu refused, %zu "
              "untranslated, %zu with other call counts, %zu lost the sun's read, %zu of %zu glass lost\n",
              folders, named, patched, rays, refused, untranslated, wrong_calls, sun_lost, glass_lost, glass);
  Check(folders == 6 && patched > 0 && refused == 0, "every pixel shader naming the lights patched");
  Check(untranslated == 0 && wrong_calls == 0, "each translated, four calls a set");
  Check(sun_lost == 0 && glass_lost == 0, "the sun's forward read and the vehicle glass's patch still taken");
}

}  // namespace

int main() {
  SyntheticChecks();
  if (const char* game = std::getenv("GTAIV_GAME_DIR"); game && *game)
    GameChecks(game);
  else
    std::printf("skipped the game's shaders: GTAIV_GAME_DIR not set\n");
  if (failures) return 1;
  std::puts("forward light patch: ok");
  return 0;
}
