// The forward shaders' cascade lookup replaced by the traced shadow's read
// (src/forward_shadow_tokens.h). Always: a synthetic ps_3_0 in the
// vanilla shape (patched: the point's scalars gathered, the read, S from
// it, every tap and the cascade's choice gone, translated to the ray
// query) and refused (no taps, a vertex shader, flow control inside the
// block, a tap feeding the colour too). With GTAIV_GAME_DIR set (the
// install), the six shader folders, parsed as tools/fxc_shaders.py does:
// - a decal's (gta_decal_dirt.fxc pixel shader 1, its draw technique) and
//   a cut-out one's (gta_cutout_fence.fxc pixel shader 1: fences and
//   foliage cards drawn forward; the trees' and grass's own pixel shaders
//   read no cascade): no read of s15 but the point's, gShadowMatrix,
//   gShadowParam* and gFacetCentre read no more, S a mov from it with its
//   saturate, the point TEXCOORD6 (v4, v3) as it was;
// - every pixel shader whose CTAB names gShadowZSamplerDir at s15 and
//   that reads it: after the sun pass's patch and this one (in the hook's
//   order), none still reads the cascade maps (ReadsCascadeMaps: what the
//   renderer counts as game shadow-map draws; none of the game's), each
//   forward one taken by the consumer (s15, gDirectionalLight's c17, away
//   from the sun) and translated with the ray query (hlsl::RayShadowCall:
//   r_rt_shadow, s15 not declared); the vehicle glass's still taking the
//   reflections' patch after it;
// - the counter's logic: the vanilla shaders all read the cascades
//   (ReadsCascadeMaps), and every tap's comparison is "add d, z, -tap.x;
//   cmp d, d, 1, 0" (the def constants resolved; but two taps of a packed
//   depth, in shaders patched), so the debug view's lit map (kLitTap) is
//   lit for every point.
// Skipped without it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "forward_shadow_tokens.h"
#include "runtime/hlsl/hlsl.h"
#include "sun_shadow_tokens.h"
#include "vehglass_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace fs = gtaiv_forward_shadow;
namespace fd = gtaiv_forward_shadow::detail;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

// A CTAB comment naming `names` (set, register, count).
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

struct Synth {
  int taps = 3;
  bool vertex = false, flow_inside = false, tap_to_colour = false;
};

// ps_3_0 in the vanilla shape: v4 the point, gShadowMatrix c60-c63, c17 the
// light; per tap "texld r5, r2, s15; add r3.w, r2.z, -r5.x; cmp r3.w, r3.w,
// c0.x, c0.y; add r3.z, r3.z, r3.w"; S "cmp_sat r2.x, r3.z, r3.z, c0.y";
// oC0 = albedo (s0 at v0) times S.
Tokens Synthetic(const Synth& o) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  const Tokens ctab = Ctab({{"TextureSampler", 3, 0, 1},
                            {"gShadowZSamplerDir", 3, 15, 1},
                            {"gDirectionalLight", 2, 17, 1},
                            {"gShadowMatrix", 2, 60, 4}});
  t.insert(t.end(), ctab.begin(), ctab.end());
  Add(t, {0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000});  // def c0, 1, 0, 0, 0
  Add(t, {0x0200001F, 0x80000005, 0x90030000});  // dcl_texcoord0 v0.xy
  Add(t, {0x0200001F, 0x80060005, 0x90070004});  // dcl_texcoord6 v4.xyz
  Add(t, {0x0200001F, 0x90000000, 0xA00F0800});  // dcl_2d s0
  Add(t, {0x0200001F, 0x90000000, 0xA00F080F});  // dcl_2d s15
  Add(t, {0x03000042, 0x800F0000, 0x90E40000, 0xA0E40800});  // texld r0, v0, s0
  Add(t, {0x03000005, 0x80070002, 0xA0F4003D, 0x90550004});  // mul r2.xyz, c61.xyww, v4.y
  Add(t, {0x04000004, 0x80070002, 0x90000004, 0xA0F4003C, 0x80E40002});  // mad r2.xyz, v4.x, c60.xyww, r2
  Add(t, {0x04000004, 0x80070002, 0x90AA0004, 0xA0F4003E, 0x80E40002});  // mad r2.xyz, v4.z, c62.xyww, r2
  Add(t, {0x03000002, 0x80070002, 0x80E40002, 0xA0F4003F});  // add r2.xyz, r2, c63.xyww
  Add(t, {0x02000001, 0x80040003, 0xA0550000});  // mov r3.z, c0.y
  if (o.flow_inside) Add(t, {0x01000026, 0xF0E40000});  // rep i0
  for (int k = 0; k < o.taps; k++) {
    Add(t, {0x03000042, 0x800F0005, 0x80E40002, 0xA0E4080F});  // texld r5, r2, s15
    Add(t, {0x03000002, 0x80080003, 0x80AA0002, 0x81000005});  // add r3.w, r2.z, -r5.x
    Add(t, {0x04000058, 0x80080003, 0x80FF0003, 0xA0000000, 0xA0550000});  // cmp r3.w, r3.w, c0.x, c0.y
    Add(t, {0x03000002, 0x80040003, 0x80AA0003, 0x80FF0003});  // add r3.z, r3.z, r3.w
  }
  if (o.flow_inside) Add(t, {0x00000027});  // endrep
  Add(t, {0x04000058, 0x80110002, 0x80AA0003, 0x80AA0003, 0xA0550000});  // cmp_sat r2.x, r3.z, r3.z, c0.y
  Add(t, {0x03000005, 0x80070000, 0x80E40000, 0x80000002});  // mul r0.xyz, r0, r2.x
  if (o.tap_to_colour) Add(t, {0x03000002, 0x80080000, 0x80E40000, 0x80000005});  // add r0.w, r0, r5.x
  Add(t, {0x02000001, 0x800F0800, 0x80E40000});  // mov oC0, r0
  Add(t, {0x0000FFFF});
  return t;
}

size_t S15Reads(const Tokens& t) {
  std::vector<fd::Ins> is;
  return fd::Parse(t, &is) ? fd::S15Reads(t, is) : size_t(-1);
}

// Instructions reading c<lo>..c<hi>.
size_t ConstReads(const Tokens& t, uint32_t lo, uint32_t hi) {
  std::vector<fd::Ins> is;
  if (!fd::Parse(t, &is)) return size_t(-1);
  size_t n = 0;
  for (const fd::Ins& in : is) {
    if (fd::Skipped(in.op)) continue;
    for (size_t k = 2; k <= in.n; k++) {
      const uint32_t r = t[in.at + k];
      if (fd::RegType(r) == fd::kConst && fd::Num(r) >= lo && fd::Num(r) <= hi) {
        n++;
        break;
      }
    }
  }
  return n;
}

bool Translates(const Tokens& t, const recomp::gfx::RayForwardShadow& s, recomp::hlsl::Program* p,
                const recomp::gfx::RayForwardReflection* r = nullptr) {
  recomp::hlsl::TranslateOptions o;
  o.ray_shadow.sampler = int(s.sampler);
  o.ray_shadow.light_register = s.light_register;
  if (r) {
    o.ray_reflection.sampler = int(r->sampler);
    o.ray_reflection.origin = r->origin_input;
    o.ray_reflection.registers[0] = r->light_register;
    o.ray_reflection.registers[1] = r->colour_register;
    o.ray_reflection.registers[2] = r->ambient_registers[0];
    o.ray_reflection.registers[3] = r->ambient_registers[1];
  }
  std::string err;
  if (!recomp::hlsl::TranslateBytecode(t.data(), t.size(), p, &err, o)) {
    std::printf("  translation: %s\n", err.c_str());
    return false;
  }
  return p->stage == recomp::hlsl::Stage::Pixel && p->ray_shadow_sampler == int(s.sampler) &&
         p->glsl.find("r_rt_shadow(gl_FragCoord, ") != std::string::npos &&
         p->glsl.find("r_s15") == std::string::npos;
}

void SyntheticChecks() {
  const Tokens orig = Synthetic({});
  Tokens t = orig;
  const bool patched = fs::PatchForwardShadowLookup(t);
  // What it should be: the point's scalars into r6 (the first free temp)
  // before each row, the read after the last, S a mov from it; the taps,
  // the rows, r3's sum gone.
  Tokens want = {0xFFFF0300u};
  {
    const Tokens ctab = Ctab({{"TextureSampler", 3, 0, 1},
                              {"gShadowZSamplerDir", 3, 15, 1},
                              {"gDirectionalLight", 2, 17, 1},
                              {"gShadowMatrix", 2, 60, 4}});
    want.insert(want.end(), ctab.begin(), ctab.end());
    Add(want, {0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000});
    Add(want, {0x0200001F, 0x80000005, 0x90030000});
    Add(want, {0x0200001F, 0x80060005, 0x90070004});
    Add(want, {0x0200001F, 0x90000000, 0xA00F0800});
    Add(want, {0x0200001F, 0x90000000, 0xA00F080F});
    Add(want, {0x03000042, 0x800F0000, 0x90E40000, 0xA0E40800});  // texld r0, v0, s0
    Add(want, {0x02000001, 0x80020006, 0x90550004});  // mov r6.y, v4.y
    Add(want, {0x02000001, 0x80010006, 0x90000004});  // mov r6.x, v4.x
    Add(want, {0x02000001, 0x80040006, 0x90AA0004});  // mov r6.z, v4.z
    Add(want, {0x03000042, 0x800F0006, 0x80A40006, 0xA0E4080F});  // texld r6, r6.xyzz, s15
    Add(want, {0x02000001, 0x80110002, 0x80000006});  // mov_sat r2.x, r6.x
    Add(want, {0x03000005, 0x80070000, 0x80E40000, 0x80000002});
    Add(want, {0x02000001, 0x800F0800, 0x80E40000});
    Add(want, {0x0000FFFF});
  }
  Check(patched && t == want, "synthetic: the point gathered, its read, S from it; the taps and rows gone");
  Check(fs::IsPatchedForwardShadow(t) && !fs::IsPatchedForwardShadow(orig) && !fs::ReadsCascadeMaps(t) &&
            fs::ReadsCascadeMaps(orig),
        "synthetic: the patched one taken, the original still reading the cascades");
  Tokens again = t;
  Check(!fs::PatchForwardShadowLookup(again) && again == t, "synthetic: a patched shader not patched again");
  recomp::gfx::RayForwardShadow s;
  recomp::hlsl::Program p;
  Check(fs::ForwardShadowConsumer(t, &s) && s.sampler == 15 && s.light_register == 17 && !s.toward_light &&
            Translates(t, s, &p) && p.glsl.find("r_rt_shadow(gl_FragCoord, r_t[6]") != std::string::npos &&
            p.glsl.find(", c[17])") != std::string::npos,
        "synthetic: the consumer's s15 and c17 (away from the sun); translated to the ray query at r6");
  Synth none, vs, flow, colour;
  none.taps = 0;
  vs.vertex = true;
  flow.flow_inside = true;
  colour.tap_to_colour = true;
  bool left = true;
  for (const Synth& o : {none, vs, flow, colour}) {
    const Tokens b = Synthetic(o);
    Tokens x = b;
    if (fs::PatchForwardShadowLookup(x) || x != b) left = false;
  }
  Check(left, "synthetic: left alone without taps, as a vertex shader, with flow control in the block, with a tap "
              "feeding the colour too");
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

const Tokens* Pixel(const std::vector<Fragment>& frags, int index) {
  for (const Fragment& f : frags)
    if (f.pixel && f.index == index) return &f.tokens;
  return nullptr;
}

// The value of source token s from the shader's def constants (component
// 0 of its swizzle, its negation); false if it is not a def's.
bool DefValue(const Tokens& t, uint32_t s, float* v) {
  if (fd::RegType(s) != fd::kConst) return false;
  std::vector<fd::Ins> is;
  if (!fd::Parse(t, &is)) return false;
  for (const fd::Ins& in : is) {
    if (in.op != fd::kDef || fd::Num(t[in.at + 1]) != fd::Num(s)) continue;
    std::memcpy(v, &t[in.at + 2 + fd::Swz(s, 0)], 4);
    const uint32_t mod = (s >> 24) & 0xF;
    if (mod == 1) *v = -*v;
    return mod == 0 || mod == 1;
  }
  return false;
}

// Every tap compared as "add d, z, -tap.x; cmp d, d, 1, 0" (lit where z is
// at least the tap), or first decoded from a packed RGBA depth ("mad d,
// tap.wxyw, ..." then frc: gbufferDepthCopy's of the nv6 and nv7 folders,
// counted in `packed`); the taps counted.
bool LitBelow(const Tokens& t, size_t* taps, size_t* packed) {
  std::vector<fd::Ins> is;
  if (!fd::Parse(t, &is)) return false;
  for (size_t k = 0; k < is.size(); k++) {
    if (!fd::ReadsSampler(t, is[k], fs::kSampler)) continue;
    (*taps)++;
    const uint32_t d = fd::Num(t[is[k].at + 1]);
    // The next instruction reading the tap's register.
    size_t j = k + 1;
    for (; j < is.size(); j++) {
      bool reads = false;
      for (size_t p = 2; p <= is[j].n && !fd::Skipped(is[j].op); p++)
        reads = reads || (fd::RegType(t[is[j].at + p]) == fd::kTemp && fd::Num(t[is[j].at + p]) == d);
      if (reads) break;
    }
    if (j + 1 < is.size() && is[j].op == fd::kMad && is[j + 1].op == fd::kFrc) {
      (*packed)++;
      continue;
    }
    if (j + 1 >= is.size() || is[j].op != fd::kAdd) return false;
    const uint32_t tap = t[is[j].at + 3], dst = t[is[j].at + 1];
    if (fd::Num(tap) != d || ((tap >> 24) & 0xF) != 1 || fd::Swz(tap, 0) != 0) return false;  // -tap.x second
    const fd::Ins& c = is[j + 1];
    if (c.op != fd::kCmp || fd::Num(t[c.at + 1]) != fd::Num(dst) || fd::Num(t[c.at + 2]) != fd::Num(dst))
      return false;
    float ge, lt;
    if (!DefValue(t, t[c.at + 3], &ge) || !DefValue(t, t[c.at + 4], &lt) || ge != 1.0f || lt != 0.0f) return false;
  }
  return true;
}

void OneShader(const char* game, const char* file, int index, const char* what) {
  const std::string path = std::string(game) + "/common/shaders/win32_30/" + file;
  std::vector<Fragment> frags;
  const Tokens* ps = Load(path, &frags) ? Pixel(frags, index) : nullptr;
  if (!ps) {
    Check(false, (path + ": there").c_str());
    return;
  }
  const Tokens orig = *ps;
  Tokens t = orig;
  gtaiv_sun_shadow::PatchSunShadowLookup(t);
  const bool patched = fs::PatchForwardShadowLookup(t);
  // The point: TEXCOORD6's input register, as the cascade's rows read it.
  bool point_v4 = false;
  std::vector<fd::Ins> is;
  uint32_t texcoord6 = 99;
  if (fd::Parse(orig, &is))
    for (const fd::Ins& in : is)
      if (in.op == fd::kDcl && (orig[in.at + 1] & 0x1F) == 5 && ((orig[in.at + 1] >> 16) & 0xF) == 6 &&
          fd::RegType(orig[in.at + 2]) == 1)
        texcoord6 = fd::Num(orig[in.at + 2]);
  if (fd::Parse(t, &is))
    for (size_t k = 3; k + 1 < is.size(); k++)
      if (fd::ReadsSampler(t, is[k], fs::kSampler)) {
        const uint32_t f = fd::Num(t[is[k].at + 1]);
        point_v4 = is[k - 3].op == fd::kMov && is[k - 2].op == fd::kMov && is[k - 1].op == fd::kMov;
        for (int r = 0; r < 3 && point_v4; r++) {
          const fd::Ins& m = is[k - 3 + size_t(r)];
          const uint32_t dst = t[m.at + 1], src = t[m.at + 2];
          point_v4 = fd::Num(dst) == f && fd::RegType(src) == 1 && fd::Num(src) == texcoord6;
        }
      }
  // S: a mov from the read's x, saturated as the cmp_sat was.
  bool s_mov = false;
  for (size_t k = 0; k < is.size(); k++)
    if (fd::ReadsSampler(t, is[k], fs::kSampler))
      for (size_t j = k + 1; j < is.size(); j++)
        if (is[j].op == fd::kMov && t[is[j].at + 2] == (0x80000000u | fd::Num(t[is[k].at + 1])))
          s_mov = ((t[is[j].at + 1] >> 20) & 1) != 0;
  const size_t cascade_reads = ConstReads(t, 53, 63);  // gShadowParam18192021 - gShadowMatrix (c53-c63)
  recomp::gfx::RayForwardShadow s;
  recomp::hlsl::Program p;
  const bool ok = patched && S15Reads(orig) == 12 && S15Reads(t) == 1 && fs::ForwardShadowConsumer(t, &s) &&
                  s.light_register == 17 && point_v4 && s_mov && cascade_reads == 0 && !fs::ReadsCascadeMaps(t) &&
                  fs::ReadsCascadeMaps(orig) && Translates(t, s, &p);
  std::printf("  %s ps%d: %zu tokens -> %zu, c53-c63 read by %zu instructions -> %zu\n", file, index, orig.size(),
              t.size(), ConstReads(orig, 53, 63), cascade_reads);
  Check(ok, what);
}

void GameChecks(const char* game) {
  OneShader(game, "gta_decal_dirt.fxc", 1,
            "a decal (gta_decal_dirt ps1): its 12 taps of s15 gone, the point's read at TEXCOORD6, S from it "
            "saturated, the cascade's constants unread, translated");
  OneShader(game, "gta_cutout_fence.fxc", 1, "a cut-out one (gta_cutout_fence ps1): likewise");
  int folders = 0;
  size_t shaders = 0, forward = 0, sun = 0, left = 0, taps = 0, packed = 0, lit_bad = 0, untranslated = 0,
         glass = 0, glass_ok = 0;
  for (const char* folder :
       {"win32_30", "win32_30_nv6", "win32_30_nv7", "win32_30_nv8", "win32_30_atidx10", "win32_30_low_ati"}) {
    const std::string dir = std::string(game) + "/common/shaders/" + folder;
    // Every .fxc there: the pixel shaders that read the cascades found by
    // their CTAB.
    std::vector<std::string> effects;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
      if (e.path().extension() == ".fxc") effects.push_back(e.path().filename().string());
    std::sort(effects.begin(), effects.end());
    bool any = false;
    for (const std::string& name : effects) {
      const char* effect = name.c_str();
      std::vector<Fragment> frags;
      if (!Load(dir + "/" + effect, &frags)) continue;
      any = true;
      for (const Fragment& f : frags) {
        if (!f.pixel || !fs::ReadsCascadeMaps(f.tokens)) continue;
        shaders++;
        if (!LitBelow(f.tokens, &taps, &packed)) lit_bad++;
        Tokens t = f.tokens;
        // The hook's order (shadows_lights.cpp, GtaivShaderTokens).
        gtaiv_sun_shadow::PatchSunShadowLookup(t);
        const bool fwd = fs::PatchForwardShadowLookup(t);
        if (gtaiv_sun_shadow::IsPatchedSunShader(t)) sun++;
        if (fs::ReadsCascadeMaps(t)) {
          left++;
          std::printf("  still reading the cascades: %s/%s ps%d\n", folder, effect, f.index);
          continue;
        }
        if (!fwd) continue;
        forward++;
        recomp::gfx::RayForwardShadow s;
        recomp::hlsl::Program p;
        const bool vehglass = std::strcmp(effect, "gta_vehicle_vehglass.fxc") == 0 && f.index >= 2 && f.index <= 4;
        recomp::gfx::RayForwardReflection r;
        if (vehglass) {
          glass++;
          if (gtaiv_vehglass::PatchVehglassLookup(t) && gtaiv_vehglass::VehglassConsumer(t, &r)) glass_ok++;
        }
        if (!fs::ForwardShadowConsumer(t, &s) || s.light_register != 17 ||
            !Translates(t, s, &p, vehglass ? &r : nullptr) ||
            (vehglass && p.glsl.find("r_rt_reflect(") == std::string::npos)) {
          untranslated++;
          std::printf("  not taken or translated: %s/%s ps%d\n", folder, effect, f.index);
        }
      }
    }
    if (any) folders++;
  }
  std::printf("  %zu pixel shaders read the cascades (%zu taps): %zu the sun pass's, %zu forward ones patched, %zu "
              "left; %zu of the vehicle glass's\n",
              shaders, taps, sun, forward, left, glass);
  Check(folders == 6, "the game's shaders in all six folders");
  Check(shaders > 0 && left == 0 && sun + forward == shaders && untranslated == 0,
        "every pixel shader reading the cascades patched (the sun pass's or forward), none left; the forward ones "
        "taken (c17) and translated with the ray query");
  Check(glass > 0 && glass_ok == glass, "the vehicle glass's still taking the reflections' patch after it");
  std::printf("  %zu taps of a packed depth\n", packed);
  Check(lit_bad == 0 && packed == 2 && taps > shaders * 11 && fs::kLitTap < -1000.0f,
        "every tap compared as z - tap >= 0 lit (1, else 0), but nv6's and nv7's gbufferDepthCopy's packed one "
        "(patched): the lit map's kLitTap lit for every point");
}

}  // namespace

int main() {
  SyntheticChecks();
  if (const char* game = std::getenv("GTAIV_GAME_DIR"); game && *game)
    GameChecks(game);
  else
    std::printf("skipped the game's shaders: GTAIV_GAME_DIR not set\n");
  if (failures) return 1;
  std::puts("forward shadow patch: ok");
  return 0;
}
