// The shadowed light volumes' atlas lookup replaced by the traced mask's
// read (src/light_shadow_tokens.h). Always: a synthetic ps_3_0 shaped as the
// game's pixel shader 14 (patched as expected: the dp4 replaced by the
// screen UV made again from the shader's own add and mul into the temp after
// the highest used, the mask's read at s14 and the mov into the dp4's
// destination; taken by the consumer's check, not patched twice; the
// consumer's answer for it only; translated) and left alone with a tap
// short or one more, a tap read by texldl, no screen UV pair or two, the
// UV's mul by another register, the mad not by dShadowParam0123's w, a dp3
// in the dp4's place, the anchor before the last tap, a CTAB name missing,
// the atlas elsewhere, no temp left, the cmp's 1 or the dp4's 0.25 another
// value, the depth read other than as its x, or a vertex shader.
// With GTAIV_GAME_DIR set (the install), in each of the six shader folders
// (all must be there): every effect's shaders offered to the patch, and
// only deferred_lighting.fxc's pixel shaders 14, 15 and 17 patched (none in
// nv6 and nv7, which unpack the depth from a colour), each once (its one
// dp4 gone, its destination the mov's, T the temp after the highest, 11
// tokens more, five reads of s14, the UV's constant a def's 0.51 as the
// shader's own G-buffer reads use it), the consumer's answer for them
// (their registers: position, 1 / radius^2, direction, cone offset and
// scale, depth) and not for the originals, and the result translates;
// deferred_lighting with the sun's, the light's and the reflections'
// patches in the game's order: each pixel shader patched by its own
// patches only (sun 1 and 2, light 14, 15 and 17, reflections 1-4), each
// consumer taking its own, all translating. Skipped without it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "light_shadow_tokens.h"
#include "reflection_tokens.h"
#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace ls = gtaiv_light_shadow;

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
Tokens Ctab(const std::vector<Entry>& entries) {
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
  bool vertex = false, short_tap = false, extra_tap = false, texldl = false, no_pair = false, two_pairs = false;
  bool other_screen = false, other_param = false, dp3 = false, early = false, no_position = false;
  bool atlas13 = false, r31 = false, cmp_two = false, dp4_half = false, packed_depth = false;
};

constexpr uint32_t kTap[4] = {0x03000042, 0x800F0006, 0x80E40002, 0xA0E4080E};  // texld r6, r2, s14
constexpr uint32_t kDp4[4] = {0x03000009, 0x80010000, 0x80E40001, 0xA0AA0000};  // dp4 r0.x, r1, c0.z

// win32_30's pixel shader 14 in small; `head` and `tail` the parts before
// and after the dp4.
Tokens Synthetic(const Synth& o, Tokens* head = nullptr, Tokens* tail = nullptr) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  std::vector<Entry> ctab = {{"GBufferTextureSampler3", 3, 4},
                             {"gShadowZSamplerCache", 3, uint16_t(o.atlas13 ? 13 : 14)},
                             {"dShadowParam0123", 2, 66},
                             {"gooDeferredLightScreenSize", 2, 84},
                             {"gDeferredLightInvSqrRadius", 2, 80},
                             {"gDeferredLightDirection", 2, 79},
                             {"gDeferredLightConeOffset", 2, 81},
                             {"gDeferredLightConeScale", 2, 82}};
  if (!o.no_position) ctab.push_back({"gDeferredLightPosition", 2, 78});
  const Tokens c = Ctab(ctab);
  t.insert(t.end(), c.begin(), c.end());
  Add(t, {0x05000051, 0xA00F0000, 0x3F028F5C, 0x44000000, 0x3E800000, 0x43800000});  // def c0, 0.51, 512, 0.25, 256
  Add(t, {0x05000051, 0xA00F0003, 0x3F800000, 0x00000000, 0x3F000000, 0xBF000000});  // def c3, 1, 0, 0.5, -0.5
  Add(t, {0x05000051, 0xA00F0005, 0x40000000, 0x3F000000, 0x00000000, 0x00000000});  // def c5, 2, 0.5, 0, 0
  Add(t, {0x0200001F, 0x80000000, 0x90031000});  // dcl vPos.xy
  Add(t, {0x0200001F, 0x90000000, 0xA00F0804});  // dcl_2d s4
  Add(t, {0x0200001F, 0x90000000, 0xA00F080E});  // dcl_2d s14
  if (!o.no_pair) {
    Add(t, {0x03000002, 0x800C0000, 0xA0000000, 0x90441000});  // add r0.zw, c0.x, vPos.xyxy
    Add(t, {0x03000005, 0x800C0000, 0x80E40000, o.other_screen ? 0xA0E40055u : 0xA0E40054u});  // mul r0.zw, r0, c84
  }
  if (o.two_pairs) {
    Add(t, {0x03000002, 0x80030003, 0x90E41000, 0xA0000000});  // add r3.xy, vPos, c0.x
    Add(t, {0x03000005, 0x80030003, 0x80E40003, 0xA0EE0054});  // mul r3.xy, r3, c84.zwzw
  }
  Add(t, {0x03000042, 0x800F0002, 0x80EE0000, 0xA0E40804});  // texld r2, r0.zwzw, s4
  Add(t, {0x04000004, 0x80010003, o.packed_depth ? 0x80E40002u : 0x80000002u, 0xA0550000,
          0xA0000000});  // mad r3.x, r2.x (r2.xyzw: packed), c0.y, c0.x
  Add(t, {0x02000001, 0x800F0001, 0xA0E40003});              // mov r1, c3
  const int taps = o.short_tap ? 3 : 4;
  auto anchor = [&](Tokens* h) {
    Add(t, {0x04000004, 0x800F0001, 0x80FF0001, o.other_param ? 0xA0FF0043u : 0xA0FF0042u,
            0x80E40006});                                                    // mad r1, r1.w, c66.w, r6
    Add(t, {0x04000058, 0x800F0001, 0x80E40001, o.cmp_two ? 0xA0000005u : 0xA0000003u,
            0xA0550003});  // cmp r1, r1, c3.x (c5.x: 2), c3.y
    if (h) *h = t;
    if (o.dp3)
      Add(t, {0x03000008, 0x80010000, 0x80E40001, 0xA0AA0000});  // dp3 r0.x, r1, c0.z
    else if (o.dp4_half)
      Add(t, {0x03000009, 0x80010000, 0x80E40001, 0xA0550005});  // dp4 r0.x, r1, c5.y (0.5)
    else
      t.insert(t.end(), std::begin(kDp4), std::end(kDp4));
  };
  if (o.early) anchor(head);
  for (int k = 0; k < taps; k++) {
    if (k == 1 && o.texldl) {
      Add(t, {0x0300005F, 0x800F0008, 0x80E40002, 0xA0E4080E});  // texldl r8, r2, s14
      continue;
    }
    Add(t, {kTap[0], k == 1 ? 0x800F0008u : kTap[1], kTap[2], kTap[3]});
  }
  if (!o.early) anchor(head);
  if (o.extra_tap) t.insert(t.end(), std::begin(kTap), std::end(kTap));
  const size_t at = t.size();
  if (o.r31) Add(t, {0x02000001, 0x800F001F, 0xA0E40003});  // mov r31, c3
  Add(t, {0x03000005, 0x800F0800, 0x80E40002, 0x80000000});  // mul oC0, r2, r0.x
  Add(t, {0x0000FFFF});
  if (tail) tail->assign(t.begin() + std::ptrdiff_t(at), t.end());
  return t;
}

bool Translates(const Tokens& t) {
  recomp::hlsl::Program p;
  std::string err;
  const bool ok = recomp::hlsl::TranslateBytecode(t.data(), t.size(), &p, &err);
  if (!ok) std::printf("  translation: %s\n", err.c_str());
  return ok;
}

void SyntheticChecks() {
  Tokens head, tail;
  Tokens t = Synthetic({}, &head, &tail);
  const Tokens original = t;
  const bool patched = ls::PatchLightShadowLookup(t);
  Tokens want = head;
  Add(want, {0x03000002, 0x80030009, 0xA0000000, 0x90441000});  // add r9.xy, c0.x, vPos.xyxy
  Add(want, {0x03000005, 0x80030009, 0x80E40009, 0xA0EE0054});  // mul r9.xy, r9, c84.zwzw
  Add(want, {0x03000042, 0x800F0009, 0x80E40009, 0xA0E4080E});  // texld r9, r9, s14
  Add(want, {0x02000001, 0x80010000, 0x80000009});              // mov r0.x, r9.x
  want.insert(want.end(), tail.begin(), tail.end());
  Check(patched && t == want,
        "synthetic: the dp4 replaced by the UV from the shader's own add and mul into r9 (after the highest temp), "
        "the mask's read at s14 and the mov into its destination");
  Check(ls::IsPatchedLightShader(t) && !ls::IsPatchedLightShader(original) && !ls::PatchLightShadowLookup(t),
        "synthetic: the patched shader recognised, the original not; not patched twice");
  recomp::gfx::RayShadow s;
  const bool took = ls::LightShadowConsumer(t, &s);
  Check(took && s.local && s.mask_sampler == 14 && s.depth_sampler == 4 && s.position_register == 78 &&
            s.radius_register == 80 && s.radius_component == 0 && s.cone && s.direction_register == 79 &&
            s.cone_offset_register == 81 && s.cone_offset_component == 0 && s.cone_scale_register == 82 &&
            s.cone_scale_component == 0 && s.lamp_gap == ls::kLampGap && !ls::LightShadowConsumer(original, &s),
        "synthetic: the consumer's answer for the patched shader (s14, s4, c78, c80.x, c79, c81.x, c82.x, the lamp's "
        "gap), none for the original");
  Check(Translates(t), "synthetic: the patched shader translates");
  // The UV pair the other way round (vPos first, .xy): copied as it is.
  {
    Tokens v = Synthetic({});
    for (size_t i = 0; i + 3 < v.size(); i++)
      if (v[i] == 0x03000002 && v[i + 1] == 0x800C0000) {
        v[i + 1] = 0x80030000, v[i + 2] = 0x90E41000, v[i + 3] = 0xA0000000;  // add r0.xy, vPos, c0.x
        v[i + 5] = 0x80030000, v[i + 7] = 0xA0EE0054;                         // mul r0.xy, r0, c84.zwzw
        break;
      }
    const bool ok = ls::PatchLightShadowLookup(v);
    bool found = false;
    for (size_t i = 0; i + 3 < v.size(); i++)
      found = found ||
              (v[i] == 0x03000002 && v[i + 1] == 0x80030009 && v[i + 2] == 0x90441000 && v[i + 3] == 0xA0000000);
    Check(ok && found, "synthetic: an add with vPos first copied in that order");
  }
  const struct {
    Synth o;
    const char* what;
  } refused[] = {
      {{.vertex = true}, "a vertex shader"},
      {{.short_tap = true}, "a tap short"},
      {{.extra_tap = true}, "a tap more"},
      {{.texldl = true}, "a tap read by texldl"},
      {{.no_pair = true}, "no screen UV pair"},
      {{.two_pairs = true}, "two screen UV pairs"},
      {{.other_screen = true}, "the UV's mul by another register"},
      {{.other_param = true}, "the mad not by dShadowParam0123's w"},
      {{.dp3 = true}, "a dp3 in the dp4's place"},
      {{.early = true}, "the anchor before the taps"},
      {{.no_position = true}, "no gDeferredLightPosition in the CTAB"},
      {{.atlas13 = true}, "the atlas at s13"},
      {{.r31 = true}, "no temp left"},
      {{.cmp_two = true}, "the cmp's 1 another value"},
      {{.dp4_half = true}, "the dp4 by 0.5, not 0.25"},
      {{.packed_depth = true}, "the depth read other than as its x (unpacked from a colour)"},
  };
  for (const auto& r : refused) {
    Tokens u = Synthetic(r.o);
    const Tokens before = u;
    Check(!ls::PatchLightShadowLookup(u) && u == before && !ls::IsPatchedLightShader(u),
          (std::string("synthetic: left alone with ") + r.what).c_str());
  }
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

std::vector<size_t> Starts(const Tokens& t) {
  std::vector<size_t> at;
  gtaiv_sun_shadow::detail::Instructions(t, &at);
  return at;
}

uint32_t RegType(uint32_t r) { return gtaiv_sun_shadow::detail::RegType(r); }

// dp4 instructions; reads of s14.
void Counts(const Tokens& t, int* dp4, int* reads) {
  *dp4 = *reads = 0;
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF;
    if (op == 0x09) ++*dp4;
    if ((op == 0x42 || op == 0x5F || op == 0x5D) && ((t[i] >> 24) & 0xF) >= 3 && RegType(t[i + 3]) == 10 &&
        (t[i + 3] & 0x7FF) == 14)
      ++*reads;
  }
}

// The value of the def'd constant `reg`'s component `c` (NaN if none).
float DefValue(const Tokens& t, uint32_t reg, uint32_t c) {
  for (size_t i : Starts(t))
    if ((t[i] & 0xFFFF) == 0x51 && RegType(t[i + 1]) == 2 && (t[i + 1] & 0x7FF) == reg) {
      float v;
      std::memcpy(&v, &t[i + 2 + c], 4);
      return v;
    }
  return std::nanf("");
}

// The registers of the light-volume shaders (by the game's CTABs): pixel
// shaders 14 and 15 position c78, 1 / radius^2 c80, direction c79, cone c81
// and c82, screen c84; 17 c79, c82, c80, c83, c84, screen c86; depth s4, s2,
// s5.
struct Regs {
  uint32_t position, radius, direction, offset, scale, screen, depth;
};
const std::map<int, Regs> kRegs = {
    {14, {78, 80, 79, 81, 82, 84, 4}}, {15, {78, 80, 79, 81, 82, 84, 2}}, {17, {79, 82, 80, 83, 84, 86, 5}}};

// One light shader's patch, as the game's: false (said) if not so.
bool LightChecks(const char* folder, const Fragment& f, const Tokens& t) {
  int dp4_0, reads_0, dp4_1, reads_1;
  Counts(f.tokens, &dp4_0, &reads_0);
  Counts(t, &dp4_1, &reads_1);
  bool shape = dp4_1 == dp4_0 - 1 && reads_0 == 4 && reads_1 == 5 && t.size() == f.tokens.size() + 11;
  // The removed dp4's destination is the mov's; T is the temp after the
  // highest the original uses.
  uint32_t dp4_dest = 0, temps = 0;
  for (size_t i : Starts(f.tokens)) {
    const uint32_t op = f.tokens[i] & 0xFFFF;
    if (op == 0x09) dp4_dest = f.tokens[i + 1];
    if (op != 0x51 && op != 0x30 && op != 0x2F)
      for (size_t n = 1; n <= ((f.tokens[i] >> 24) & 0xF); n++)
        if (!(op == 0x1F && n == 1) && RegType(f.tokens[i + n]) == 0)
          temps = std::max(temps, (f.tokens[i + n] & 0x7FF) + 1);
  }
  bool mov = false;
  const std::vector<size_t> starts = Starts(t);
  for (size_t k = 0; k + 1 < starts.size(); k++) {
    const size_t i = starts[k], v = starts[k + 1];
    if (t[i] == 0x03000042u && (t[i + 3] & 0x7FF) == 14 && RegType(t[i + 3]) == 10 && t[v] == 0x02000001u)
      mov = mov || (t[v + 1] == dp4_dest && (t[i + 1] & 0x7FF) == temps && (t[v + 2] & 0x7FF) == temps);
  }
  shape = shape && mov;
  // The inserted add: c<K>.k with the def's 0.51 (the shader's own UV
  // offset), and the mul by the screen size's zw.
  const auto it = kRegs.find(f.index);
  bool uv = false;
  const std::vector<size_t> at = Starts(t);
  for (size_t k = 0; k + 3 < at.size() && it != kRegs.end(); k++) {
    const size_t i = at[k], m = at[k + 1], x = at[k + 2], v = at[k + 3];
    if (t[i] != 0x03000002u || t[m] != 0x03000005u || t[x] != 0x03000042u || t[v] != 0x02000001u) continue;
    const uint32_t c = RegType(t[i + 2]) == 2 ? t[i + 2] : t[i + 3];
    const float k_value = DefValue(t, c & 0x7FF, (c >> 16) & 3);
    uv = uv || (std::fabs(k_value - 0.51f) < 1e-6f && t[m + 3] == (0xA0EE0000u | it->second.screen) &&
                (t[x + 3] & 0x7FF) == 14);
  }
  recomp::gfx::RayShadow s;
  const bool took = ls::LightShadowConsumer(t, &s);
  bool regs = false;
  if (took && it != kRegs.end()) {
    const Regs& r = it->second;
    regs = s.local && s.cone && s.mask_sampler == 14 && s.depth_sampler == r.depth &&
           s.position_register == r.position && s.radius_register == r.radius && s.radius_component == 0 &&
           s.direction_register == r.direction && s.cone_offset_register == r.offset &&
           s.cone_scale_register == r.scale && s.cone_offset_component == 0 && s.cone_scale_component == 0;
  }
  recomp::gfx::RayShadow none;
  const bool original = !ls::LightShadowConsumer(f.tokens, &none) && !ls::IsPatchedLightShader(f.tokens);
  const bool translated = Translates(t);
  if (!(shape && uv && took && regs && original && translated))
    std::printf("  %s: pixel shader %d: shape %d (dp4 %d -> %d, s14 %d -> %d, %zu -> %zu tokens), uv %d, consumer "
                "%d, registers %d, original refused %d, translated %d\n",
                folder, f.index, shape, dp4_0, dp4_1, reads_0, reads_1, f.tokens.size(), t.size(), uv, took, regs,
                original, translated);
  return shape && uv && took && regs && original && translated;
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
    // Every effect's shaders offered: (effect, pixel shader) patched.
    std::set<std::pair<std::string, int>> patched;
    bool ok = true, lighting = false;
    int effects = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".fxc") continue;
      std::vector<Fragment> frags;
      if (!Fragments(Read(e.path()), &frags)) continue;
      effects++;
      const std::string name = e.path().stem().string();
      lighting = lighting || name == "deferred_lighting";
      for (const Fragment& f : frags) {
        Tokens t = f.tokens;
        if (!ls::PatchLightShadowLookup(t)) {
          if (ls::IsPatchedLightShader(f.tokens)) ok = false;
          continue;
        }
        patched.insert({name, f.pixel ? f.index : -1 - f.index});
        if (name == "deferred_lighting" && f.pixel) ok = LightChecks(folder, f, t) && ok;
      }
    }
    // (nv6 and nv7 unpack the depth from a colour: left alone.)
    const bool packed = std::string(folder) == "win32_30_nv6" || std::string(folder) == "win32_30_nv7";
    const std::set<std::pair<std::string, int>> want =
        packed ? std::set<std::pair<std::string, int>>{}
               : std::set<std::pair<std::string, int>>{
                     {"deferred_lighting", 14}, {"deferred_lighting", 15}, {"deferred_lighting", 17}};
    for (const auto& [name, index] : patched)
      if (!want.count({name, index})) std::printf("  %s: %s shader %d patched too\n", folder, name.c_str(), index);
    std::printf("  %s: %d effects offered\n", folder, effects);
    Check(ok && lighting && patched == want,
          (std::string(folder) + (packed ? ": of every effect, none patched (deferred_lighting's 14, 15 and 17 "
                                           "unpack the depth from a colour)"
                                         : ": of every effect, deferred_lighting's pixel shaders 14, 15 and 17 only "
                                           "patched, each once (the dp4 gone, its destination the mov's, T the temp "
                                           "after the highest, five reads of s14, the UV with the shader's 0.51 and "
                                           "screen size), the consumer's registers, the originals refused, "
                                           "translated"))
              .c_str());

    // The ray-traced variant (PatchLightShadowRay): the same three shaders,
    // one read of s14 left, the consumer's point light, and the translation
    // with the ray option making it one r_rt_shadow call (no s14 sampled).
    {
      std::vector<Fragment> frags;
      Fragments(Read(dir / "deferred_lighting.fxc"), &frags);
      std::set<int> rays;
      bool good = true;
      for (const Fragment& f : frags) {
        if (!f.pixel) continue;
        Tokens t = f.tokens;
        if (!ls::PatchLightShadowRay(t)) {
          good = good && !ls::IsRayPatchedLightShader(f.tokens);
          continue;
        }
        rays.insert(f.index);
        recomp::gfx::RayForwardShadow fs;
        recomp::gfx::RayShadow ms;
        const bool took = ls::LightShadowRayConsumer(t, &fs);
        good = good && ls::IsRayPatchedLightShader(t) && !ls::IsPatchedLightShader(t) &&
               !ls::LightShadowConsumer(t, &ms) && took && fs.point && fs.toward_light && fs.sampler == 14 &&
               fs.lamp_gap == ls::kLampGap && !ls::PatchLightShadowRay(t);
        // The light's reach and the depth, for the renderer's per-pixel
        // masks: the same registers the mask variant's consumer names.
        ls::Ctab ctab;
        good = good && ls::ReadCtab(t, &ctab) && fs.local && fs.cone && fs.depth_sampler == uint32_t(ctab.depth) &&
               fs.light_register == uint32_t(ctab.position) && fs.radius_register == uint32_t(ctab.inv_sq_radius) &&
               fs.radius_component == 0 && fs.direction_register == uint32_t(ctab.direction) &&
               fs.cone_offset_register == uint32_t(ctab.cone_offset) && fs.cone_offset_component == 0 &&
               fs.cone_scale_register == uint32_t(ctab.cone_scale) && fs.cone_scale_component == 0;
        recomp::hlsl::Program prog;
        std::string err;
        recomp::hlsl::TranslateOptions o;
        o.ray_shadow.sampler = int(fs.sampler);
        o.ray_shadow.light_register = fs.light_register;
        const bool tr = recomp::hlsl::TranslateBytecode(t.data(), t.size(), &prog, &err, o);
        if (!tr) std::printf("  %s ray ps%d translation: %s\n", folder, f.index, err.c_str());
        size_t calls = 0;
        for (size_t at = prog.glsl.find("r_rt_shadow(gl_FragCoord"); at != std::string::npos;
             at = prog.glsl.find("r_rt_shadow(gl_FragCoord", at + 1))
          calls++;
        good = good && tr && calls == 1 && prog.glsl.find("texture(r_s14") == std::string::npos;
      }
      Check(good && rays == (packed ? std::set<int>{} : std::set<int>{14, 15, 17}),
            (std::string(folder) + ": the lights traced by their own pixels: deferred_lighting's 14, 15 and 17 "
                                   "(none in nv6 and nv7), one ray toward the light's position each, the "
                                   "consumer's point light with its reach, cone and depth, the originals refused, "
                                   "not patched twice")
                .c_str());
    }

    // deferred_lighting with every patch of the lighting, in the game's
    // order (shadows_lights.cpp's GtaivShaderTokens).
    std::vector<Fragment> frags;
    Fragments(Read(dir / "deferred_lighting.fxc"), &frags);
    std::set<int> sun, light, refl;
    bool consumers = true, translated = true;
    for (const Fragment& f : frags) {
      if (!f.pixel) continue;
      Tokens t = f.tokens;
      if (gtaiv_sun_shadow::PatchSunShadowLookup(t)) sun.insert(f.index);
      if (ls::PatchLightShadowLookup(t)) light.insert(f.index);
      if (gtaiv_reflection::PatchReflectionLookup(t)) refl.insert(f.index);
      if (t != f.tokens) translated = Translates(t) && translated;
      recomp::gfx::RayShadow s;
      recomp::gfx::RayReflection r;
      const bool l = ls::LightShadowConsumer(t, &s), u = gtaiv_sun_shadow::IsPatchedSunShader(t);
      const bool x = gtaiv_reflection::ReflectionConsumer(t, &r);
      consumers = consumers && l == light.count(f.index) > 0 && u == sun.count(f.index) > 0 &&
                  x == refl.count(f.index) > 0 && !(l && u);
    }
    Check(sun == std::set<int>{1, 2} && light == (packed ? std::set<int>{} : std::set<int>{14, 15, 17}) &&
              refl == std::set<int>{1, 2, 3, 4} && consumers && translated,
          (std::string(folder) + ": deferred_lighting with the sun's, the lights' and the reflections' patches: sun "
                                 "1 and 2, lights 14, 15 and 17 (none in nv6 and nv7), reflections 1-4, each "
                                 "consumer taking its own, all translating")
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
  std::puts("light shadow patch: ok");
  return 0;
}
