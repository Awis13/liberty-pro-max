// The sun pass's shadow lookup replaced by the traced mask's read
// (src/sun_shadow_tokens.h). Always: a synthetic ps_3_0 built from the
// block's first and last instructions (patched as expected; left alone with
// a tap short, a tap outside the block, another CTAB, a vertex shader, the
// block's ends swapped or twice). With GTAIV_GAME_DIR set (the install):
// the game's own deferred_lighting.fxc of each of the six shader folders
// (all must be there), parsed as
// tools/fxc_shaders.py does: the patch applies to pixel shaders 1 and 2
// only, removes all 12 taps of s15 there, keeps the linear depth (rcp
// r0.x, r0.x), puts in the mask's read, and the result translates
// (hlsl::TranslateBytecode); their G-buffer normals (the rays' start) at s1;
// skipped without it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace ss = gtaiv_sun_shadow;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

// A CTAB comment naming sampler `a` at s`ra` and `b` at s`rb`.
Tokens Ctab(const char* a, uint16_t ra, const char* b, uint16_t rb) {
  std::vector<uint8_t> t(28 + 2 * 20, 0);
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&t[at], &v, 4); };
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&t[at], &v, 2); };
  auto str = [&](const char* s) {
    const uint32_t at = uint32_t(t.size());
    t.insert(t.end(), s, s + std::strlen(s) + 1);
    return at;
  };
  put32(0, 28);
  put32(8, 0xFFFF0300u);
  put32(12, 2);
  put32(16, 28);
  const uint32_t na = str(a), nb = str(b), target = str("ps_3_0"), creator = str("test");
  put32(4, creator);
  put32(24, target);
  const uint32_t names[2] = {na, nb};
  const uint16_t regs[2] = {ra, rb};
  for (int i = 0; i < 2; i++) {
    put32(28 + i * 20, names[i]);
    put16(28 + i * 20 + 4, 3);  // D3DXRS_SAMPLER
    put16(28 + i * 20 + 6, regs[i]);
    put16(28 + i * 20 + 8, 1);
  }
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
void Add(Tokens& t, const uint32_t* v, size_t n) { t.insert(t.end(), v, v + n); }

constexpr uint32_t kTap[4] = {0x03000042, 0x800F0002, 0x80E40002, 0xA0E4080F};  // texld r2, r2, s15
constexpr uint32_t kCompare[4] = {0x03000002, 0x80080001, 0x80AA0001, 0x81000002};  // add r1.w, r1.z, -r2.x
constexpr uint32_t kRcp[3] = {0x02000006, 0x80010000, 0x80000000};               // rcp r0.x, r0.x

struct Synth {
  int taps_inside = 12, taps_after = 0;
  bool swap_ends = false, two_starts = false, vertex = false;
  uint16_t shadow_reg = 15;
};

// The shader before the block, the block, after it; `before` and `after`
// the parts a patch keeps.
Tokens Synthetic(const Synth& o, Tokens* before = nullptr, Tokens* after = nullptr) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  const Tokens ctab = Ctab("gShadowZSamplerDir", o.shadow_reg, "GBufferTextureSampler3", 4);
  Add(t, ctab.data(), ctab.size());
  Add(t, {0x0200001F, 0x80000005, 0x90030000});  // dcl_texcoord0 v0.xy
  Add(t, {0x0200001F, 0x80010005, 0x900F0001});  // dcl_texcoord1 v1
  Add(t, {0x0200001F, 0x90000000, 0xA00F0804});  // dcl_2d s4
  Add(t, {0x0200001F, 0x90000000, 0xA00F080F});  // dcl_2d s15
  Add(t, {0x03000042, 0x800F0000, 0x90E40000, 0xA0E40804});  // texld r0, v0, s4
  Add(t, kRcp, 3);
  if (before) *before = t;
  Add(t, o.swap_ends ? ss::kEnd : ss::kStart, 5);
  if (o.two_starts) Add(t, ss::kStart, 5);
  for (int i = 0; i < o.taps_inside; i++) {
    Add(t, kTap, 4);
    Add(t, kCompare, 4);
  }
  Add(t, o.swap_ends ? ss::kStart : ss::kEnd, 5);
  const size_t at = t.size();
  for (int i = 0; i < o.taps_after; i++) Add(t, kTap, 4);
  Add(t, {0x02000001, 0x800F0800, 0x80E40000});  // mov oC0, r0
  Add(t, {0x0000FFFF});
  if (after) after->assign(t.begin() + std::ptrdiff_t(at), t.end());
  return t;
}

void SyntheticChecks() {
  Tokens before, after;
  Tokens t = Synthetic({}, &before, &after);
  Tokens want = before;
  Add(want, ss::kMaskRead, 7);
  Add(want, after.data(), after.size());
  const bool patched = ss::PatchSunShadowLookup(t);
  Check(patched && t == want, "synthetic: the block from mad r0.yzw to cmp_sat r0.y replaced by the mask's read");
  Check(ss::IsPatchedSunShader(t) && !ss::IsPatchedSunShader(Synthetic({})),
        "synthetic: the consumer takes the patched shader, not the original");
  Tokens again = t;
  Check(!ss::PatchSunShadowLookup(again) && again == t, "synthetic: a patched shader not patched again");
  struct Bad {
    Synth s;
    const char* what;
  };
  Synth eleven, outside, other_reg, vs, swapped, twice;
  eleven.taps_inside = 11;
  outside.taps_inside = 11;
  outside.taps_after = 1;
  other_reg.shadow_reg = 14;
  vs.vertex = true;
  swapped.swap_ends = true;
  twice.two_starts = true;
  const Bad kBad[] = {{eleven, "11 taps"}, {outside, "a tap outside the block"}, {other_reg, "the shadow map at s14"},
                      {vs, "a vertex shader"}, {swapped, "the ends swapped"}, {twice, "the start twice"}};
  bool left = true;
  for (const Bad& b : kBad) {
    const Tokens orig = Synthetic(b.s);
    Tokens x = orig;
    if (ss::PatchSunShadowLookup(x) || x != orig) {
      std::printf("  patched: %s\n", b.what);
      left = false;
    }
  }
  Check(left, "synthetic: left alone with a tap short, one outside the block, another CTAB, a vertex shader, the "
              "ends swapped or twice");
}

// The cascade lookup's registers as the vanilla shaders use them.
bool Cascade(const Tokens& t) {
  ss::CascadeRegisters c;
  return ss::SunShadowCascade(t, &c) && c.sampler == 15 && c.view == 14 && c.split == 54 && c.matrix == 60 &&
         c.range == 53 && c.select[0] == 57 && c.select[1] == 58 && c.select[2] == 59 && c.select[3] == 56;
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

bool Contains(const Tokens& t, const uint32_t* seq, size_t n) {
  for (size_t i = 0; i + n <= t.size(); i++)
    if (std::memcmp(&t[i], seq, n * 4) == 0) return true;
  return false;
}

// dp3 instructions reading c17 (gDirectionalLight): negated, and not.
void C17Dots(const Tokens& t, int* negated, int* plain) {
  *negated = *plain = 0;
  for (size_t i = 1; i < t.size();) {
    const uint32_t op = t[i] & 0xFFFF;
    if (op == 0xFFFF) break;
    const size_t n = op == 0xFFFE ? (t[i] >> 16) & 0x7FFF : (t[i] >> 24) & 0xF;
    if (op == 8)  // dp3
      for (size_t k = 2; k <= n && i + k < t.size(); k++) {
        const uint32_t s = t[i + k];
        const uint32_t type = ((s >> 28) & 7) | ((s >> 8) & 0x18);
        if (type == 2 && (s & 0x7FF) == ss::kLightRegister) (((s >> 24) & 0xF) == 1 ? *negated : *plain)++;
      }
    i += 1 + n;
  }
}

size_t S15Taps(const Tokens& t) {
  size_t n = 0;
  for (size_t i = 0; i + 3 < t.size(); i++)
    if (t[i] == 0x03000042 && (t[i + 3] & 0x7FF) == 15 && ((t[i + 3] >> 28) & 7) == 2 &&
        ((t[i + 3] >> 8) & 0x18) == 8)
      n++;
  return n;
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
    bool ok = true;
    for (const Fragment& f : frags) {
      Tokens t = f.tokens;
      if (!ss::PatchSunShadowLookup(t)) {
        if (ss::IsPatchedSunShader(f.tokens)) ok = false;
        continue;
      }
      if (!f.pixel) {
        ok = false;
        continue;
      }
      patched.insert(f.index);
      // The sun's direction as the lighting reads it: only ever negated
      // in its dot products (N . -c17), so c17 points away from the sun:
      // the consumer's toward_light false.
      int negated = 0, plain = 0;
      C17Dots(t, &negated, &plain);
      const bool shape = negated >= 2 && plain == 0 && S15Taps(f.tokens) == ss::kTaps && S15Taps(t) == 1 &&
                         Contains(t, ss::kMaskRead, 7) &&
                         Contains(t, kRcp, 3) && !Contains(t, ss::kStart, 5) && !Contains(t, ss::kEnd, 5) &&
                         ss::IsPatchedSunShader(t) && ss::SunShadowNormalSampler(t) == 1 &&
                         ss::SunShadowColourRegister(t) == 18 && Cascade(t);
      recomp::hlsl::Program prog;
      std::string err;
      const bool translated = recomp::hlsl::TranslateBytecode(t.data(), t.size(), &prog, &err);
      if (!translated) std::printf("  %s ps%d: %s\n", folder, f.index, err.c_str());
      std::printf("  %s ps%d: %zu tokens -> %zu\n", folder, f.index, f.tokens.size(), t.size());
      ok = ok && shape && translated && prog.stage == recomp::hlsl::Stage::Pixel;
    }
    const std::string what = std::string(folder) +
                             ": pixel shaders 1 and 2 only patched, their 12 taps of s15 gone, the linear depth "
                             "kept, the mask read, translated; c17 only negated in their dot products (it points "
                             "away from the sun); the G-buffer's normals at s1; the sun's colour at c18; the "
                             "cascade lookup's registers (s15, c14, c54, c57 c58 c59 c56, c60, c53)";
    Check(ok && patched == std::set<int>{1, 2}, what.c_str());
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
  std::puts("sun shadow patch: ok");
  return 0;
}
