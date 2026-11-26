// Vehicle glass tracing its own reflections (src/vehglass_tokens.h).
// Always: a synthetic ps_3_0 shaped as the game's (patched as expected: the
// reflected direction made from the eye (gViewInverse's fourth row), the
// world position (TEXCOORD2) and the normal (TEXCOORD1), its read of s3 and
// the blend after the environment map's texldl, the fade's max after its
// mul_sat, dcl_2d s3 after the other declarations, the two temps after the
// highest used; taken by the consumer's check, not patched twice; the
// consumer's answer for it only; translated with the consumer's answer,
// s3's read the ray query's call) and left alone where s3 is used, the
// environment map is read twice or by texld, the muls are not the fade's,
// the fade's direction is not a .z, the CTAB lacks a name the patch or the
// consumer reads or names emissiveMultiplier (lamps' lenses), TEXCOORD1 or
// 2 is not declared, or it is a vertex shader.
// With GTAIV_GAME_DIR set (the install), in each of the six shader folders
// (all must be there): every effect's pixel shaders offered to the patch,
// and only gta_vehicle_vehglass.fxc's 2, 3 and 4 patched (not
// gta_vehicle_lightsemissive.fxc's, the same shape), each once (one dcl_2d
// s3, one read of s3, the texldl kept, 39 tokens more, the temps the
// shader leaves), the consumer's answer for them and not for the originals
// (s3, v4; c17, c18, c37, c38), the eye c15 and the normal v1 read, the
// term times a def's 10 after the texldl (the output's scale, 1 / 10), and
// the result translates with the consumer's answer (hlsl::TranslateBytecode:
// s3's read the call, s3 not declared); skipped without it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "runtime/hlsl/hlsl.h"
#include "vehglass_tokens.h"

namespace {

using Tokens = std::vector<uint32_t>;
namespace gv = gtaiv_vehglass;

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
  bool vertex = false, s3 = false, twice = false, texld = false, swapped = false, not_z = false, no_env = false;
  bool no_colour = false, no_origin = false, other_dest = false, emissive = false, no_normal = false, no_eye = false;
};

// win32_30's pixel shader 2 in small: R in r5, the environment map's
// texldl into r4, the fade's mul_sat and mul; `head`, `middle` and `tail`
// the parts around the declaration, the blend and the max a patch puts in.
Tokens Synthetic(const Synth& o, Tokens* head = nullptr, Tokens* middle = nullptr, Tokens* fade = nullptr,
                 Tokens* tail = nullptr) {
  Tokens t = {o.vertex ? 0xFFFE0300u : 0xFFFF0300u};
  const Tokens ctab = Ctab({{"TextureSampler", 3, 0},
                            {o.no_env ? "OtherSampler" : "EnvironmentSampler", 3, 4},
                            {o.no_eye ? "gViewOther" : "gViewInverse", 2, 12},
                            {"gDirectionalLight", 2, 17},
                            {o.no_colour ? "gOtherColour" : "gDirectionalColour", 2, 18},
                            {"gLightAmbient0", 2, 37},
                            {"gLightAmbient1", 2, 38},
                            {o.emissive ? "emissiveMultiplier" : "dirtLevel", 2, 72}});
  t.insert(t.end(), ctab.begin(), ctab.end());
  Add(t, {0x05000051, 0xA00F0003, 0x40A00000, 0x41200000, 0x3FE66666, 0xBE800000});  // def c3, 5, 10, 1.8, -0.25
  Add(t, {0x0200001F, 0x80000005, 0x90030000});  // dcl_texcoord0 v0.xy
  Add(t, {0x0200001F, o.no_normal ? 0x80040005u : 0x80010005u, 0x900F0001});  // dcl_texcoord1 v1
  Add(t, {0x0200001F, o.no_origin ? 0x80030005u : 0x80020005u, 0x90070004});  // dcl_texcoord2 v4.xyz
  Add(t, {0x0200001F, 0x90000000, 0xA00F0800});  // dcl_2d s0
  Add(t, {0x0200001F, 0x90000000, 0xA00F0804});  // dcl_2d s4
  if (o.s3) Add(t, {0x0200001F, 0x90000000, 0xA00F0803});  // dcl_2d s3
  if (head) *head = t;
  const size_t mid = t.size();
  Add(t, {0x03000042, 0x800F0000, 0x90E40000, 0xA0E40800});  // texld r0, v0, s0
  Add(t, {0x02000024, 0x80070005, 0x90E40004});              // nrm r5.xyz, v4
  if (o.twice) Add(t, {0x0300005F, 0x800F0006, 0x80E40005, 0xA0E40804});  // texldl r6, r5, s4
  Add(t, {o.texld ? 0x03000042u : 0x0300005Fu, 0x800F0004, 0x80E40005, 0xA0E40804});  // texldl r4, r5, s4
  if (middle) middle->assign(t.begin() + std::ptrdiff_t(mid), t.end());
  const size_t f = t.size();
  Add(t, {0x03000005, 0x80110001, o.not_z ? 0x80550005u : 0x80AA0005u, 0xA0000003});  // mul_sat r1.x, r5.z, c3.x
  if (fade) fade->assign(t.begin() + std::ptrdiff_t(f), t.end());
  const size_t at = t.size();
  if (o.swapped)
    Add(t, {0x03000005, 0x80070004, 0x80000001, 0x80E40004});  // mul r4.xyz, r1.x, r4
  else if (o.other_dest)
    Add(t, {0x03000005, 0x80070006, 0x80E40004, 0x80000001});  // mul r6.xyz, r4, r1.x
  else
    Add(t, {0x03000005, 0x80070004, 0x80E40004, 0x80000001});  // mul r4.xyz, r4, r1.x
  Add(t, {0x03000005, 0x80070004, 0x80E40004, 0xA0550003});  // mul r4.xyz, r4, c3.y
  Add(t, {0x03000002, 0x800F0800, 0x80E40000, 0x80E40004});  // add oC0, r0, r4
  Add(t, {0x0000FFFF});
  if (tail) tail->assign(t.begin() + std::ptrdiff_t(at), t.end());
  return t;
}

// The translation with the consumer's answer, as x86recomp's d3d9 makes it.
bool TranslateAsTraced(const Tokens& t, const recomp::gfx::RayForwardReflection& r, recomp::hlsl::Program* p,
                       std::string* err) {
  recomp::hlsl::TranslateOptions o;
  o.ray_reflection.sampler = int(r.sampler);
  o.ray_reflection.origin = r.origin_input;
  o.ray_reflection.registers[0] = r.light_register;
  o.ray_reflection.registers[1] = r.colour_register;
  o.ray_reflection.registers[2] = r.ambient_registers[0];
  o.ray_reflection.registers[3] = r.ambient_registers[1];
  return recomp::hlsl::TranslateBytecode(t.data(), t.size(), p, err, o);
}

bool Traced(const recomp::hlsl::Program& p, uint32_t origin) {
  const std::string call = "r_rt_reflect(gl_FragCoord, r_v[" + std::to_string(origin) + "], ";
  bool s3 = false;
  for (const auto& s : p.samplers) s3 = s3 || s.reg == 3;
  return p.ray_reflection_sampler == 3 && !s3 && p.glsl.find(call) != std::string::npos &&
         p.glsl.find(", c[17], c[18], c[37], c[38])") != std::string::npos;
}

void SyntheticChecks() {
  Tokens head, middle, fade, tail;
  Tokens t = Synthetic({}, &head, &middle, &fade, &tail);
  Tokens want = head;
  Add(want, {0x0200001F, 0x90000000, 0xA00F0803});  // dcl_2d s3
  want.insert(want.end(), middle.begin(), middle.end());
  Add(want, {0x03000002, 0x80070006, 0xA0E4000F, 0x91E40004});              // add r6.xyz, c15, -v4
  Add(want, {0x02000024, 0x80070006, 0x80E40006});                          // nrm r6.xyz, r6
  Add(want, {0x02000024, 0x80070007, 0x90E40001});                          // nrm r7.xyz, v1
  Add(want, {0x03000008, 0x80080007, 0x80E40007, 0x80E40006});              // dp3 r7.w, r7, r6
  Add(want, {0x03000002, 0x80080007, 0x80FF0007, 0x80FF0007});              // add r7.w, r7.w, r7.w
  Add(want, {0x04000004, 0x80070006, 0x80E40007, 0x80FF0007, 0x81E40006});  // mad r6.xyz, r7, r7.w, -r6
  Add(want, {0x03000042, 0x800F0006, 0x80A40006, 0xA0E40803});              // texld r6, r6.xyzz, s3
  Add(want, {0x04000012, 0x80070004, 0x80FF0006, 0x80E40006, 0x80E40004});  // lrp r4.xyz, r6.w, r6, r4
  want.insert(want.end(), fade.begin(), fade.end());
  Add(want, {0x0300000B, 0x80010001, 0x80000001, 0x80FF0006});  // max r1.x, r1.x, r6.w
  want.insert(want.end(), tail.begin(), tail.end());
  const bool patched = gv::PatchVehglassLookup(t);
  Check(patched && t == want,
        "synthetic: dcl_2d s3, after the texldl R from the eye, position and normal, the read of s3 along it into "
        "the next free temp and the blend, the fade's max");
  Check(gv::IsPatchedVehglassShader(t) && !gv::IsPatchedVehglassShader(Synthetic({})),
        "synthetic: the consumer takes the patched shader, not the original");
  Tokens again = t;
  Check(!gv::PatchVehglassLookup(again) && again == t, "synthetic: a patched shader not patched again");
  struct Bad {
    Synth s;
    const char* what;
  };
  Synth vs, s3, twice, texld, swapped, not_z, no_env, no_colour, no_origin, other_dest, emissive, no_normal, no_eye;
  emissive.emissive = true;
  no_normal.no_normal = true;
  no_eye.no_eye = true;
  vs.vertex = true;
  s3.s3 = true;
  twice.twice = true;
  texld.texld = true;
  swapped.swapped = true;
  not_z.not_z = true;
  no_env.no_env = true;
  no_colour.no_colour = true;
  no_origin.no_origin = true;
  other_dest.other_dest = true;
  const Bad kBad[] = {{vs, "a vertex shader"},
                      {s3, "s3 declared"},
                      {twice, "the environment map read twice"},
                      {texld, "the environment map read by texld"},
                      {swapped, "the mul's operands swapped"},
                      {not_z, "the fade not of a .z"},
                      {no_env, "no EnvironmentSampler"},
                      {no_colour, "no gDirectionalColour"},
                      {no_origin, "TEXCOORD3, not TEXCOORD2"},
                      {other_dest, "the mul writing another register"},
                      {emissive, "emissiveMultiplier (a lamp's lens)"},
                      {no_normal, "TEXCOORD4, not TEXCOORD1"},
                      {no_eye, "no gViewInverse"}};
  bool left = true;
  for (const Bad& b : kBad) {
    const Tokens orig = Synthetic(b.s);
    Tokens x = orig;
    if (gv::PatchVehglassLookup(x) || x != orig) {
      std::printf("  patched: %s\n", b.what);
      left = false;
    }
  }
  Check(left, "synthetic: left alone with s3 used, the map read twice or by texld, other muls, a fade not of R.z, a "
              "CTAB without EnvironmentSampler, gViewInverse or another name the consumer reads, one naming "
              "emissiveMultiplier, no TEXCOORD1 or 2, a vertex shader");
  recomp::gfx::RayForwardReflection r;
  Check(gv::VehglassConsumer(t, &r) && r.sampler == 3 && r.origin_input == 4 && r.light_register == 17 &&
            !r.toward_light && r.colour_register == 18 && r.ambient_registers[0] == 37 &&
            r.ambient_registers[1] == 38 && r.up[2] == 1 && r.diffuse_wrap == 0.25f && r.output_scale == 0.1f &&
            r.max_distance == 100 && !gv::VehglassConsumer(Synthetic({}), &r) &&
            !gv::VehglassConsumer(Synthetic(no_colour), &r),
        "synthetic: the consumer's answer for the patched shader only");
  recomp::hlsl::Program p;
  std::string err;
  gv::VehglassConsumer(t, &r);
  Check(TranslateAsTraced(t, r, &p, &err) && Traced(p, 4), "synthetic: translated with s3's read the ray query's call");
}

// ---- the game's gta_vehicle_vehglass.fxc

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

// Declarations of s3, reads of s3, texldl from sampler `env`, the highest
// temp + 1.
void Counts(const Tokens& t, int env, int* dcls, int* reads, int* texldl, uint32_t* temps) {
  *dcls = *reads = *texldl = 0;
  *temps = 0;
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF;
    const uint32_t n = (t[i] >> 24) & 0xF;
    if (op == 0x1F && n == 2 && t[i + 1] == 0x90000000u && RegType(t[i + 2]) == 10 && (t[i + 2] & 0x7FF) == 3)
      (*dcls)++;
    if ((op == 0x42 || op == 0x5F) && n == 3 && RegType(t[i + 3]) == 10) {
      if ((t[i + 3] & 0x7FF) == 3) (*reads)++;
      if (op == 0x5F && int(t[i + 3] & 0x7FF) == env) (*texldl)++;
    }
    if (op == 0x51 || op == 0x30 || op == 0x2F) continue;
    for (uint32_t k = 1; k <= n; k++)
      if (!(op == 0x1F && k == 1) && RegType(t[i + k]) == 0 && (t[i + k] & 0x7FF) + 1 > *temps)
        *temps = (t[i + k] & 0x7FF) + 1;
  }
}

// Whether, after the environment map's texldl, a mul by a def's constant
// 10 (replicated) scales the term: the output's scale (gv::kTermScale).
bool TimesTen(const Tokens& t, int env) {
  std::vector<uint32_t> tens;  // register * 4 + lane of the def constants equal to 10
  bool after = false;
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF;
    if (op == 0x51) {
      for (int k = 0; k < 4; k++) {
        float v;
        std::memcpy(&v, &t[i + 2 + k], 4);
        if (v == gv::kTermScale) tens.push_back((t[i + 1] & 0x7FF) * 4 + uint32_t(k));
      }
      continue;
    }
    if (op == 0x5F && RegType(t[i + 3]) == 10 && int(t[i + 3] & 0x7FF) == env) after = true;
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

// Whether the tokens read c<eye> and v<normal> (any instruction's source).
bool Reads(const Tokens& t, uint32_t type, uint32_t n) {
  for (size_t i : Starts(t)) {
    const uint32_t op = t[i] & 0xFFFF, len = (t[i] >> 24) & 0xF;
    if (op == 0x1F || op == 0x51) continue;
    for (uint32_t k = 2; k <= len; k++)
      if (RegType(t[i + k]) == type && (t[i + k] & 0x7FF) == n) return true;
  }
  return false;
}

// One glass shader's patch, as the game's: false (said) if not so.
bool GlassChecks(const char* folder, const Fragment& f, const Tokens& t) {
  const int env = gtaiv_shader::CtabRegister(f.tokens, "EnvironmentSampler", gtaiv_shader::kSamplerSet);
  int dcls, reads, texldl, dcls0, reads0, texldl0;
  uint32_t temps, temps0;
  Counts(f.tokens, env, &dcls0, &reads0, &texldl0, &temps0);
  Counts(t, env, &dcls, &reads, &texldl, &temps);
  Tokens again = t;
  recomp::gfx::RayForwardReflection r;
  const bool ctab = gv::VehglassConsumer(t, &r) && !gv::VehglassConsumer(f.tokens, &r) &&
                    gv::VehglassConsumer(t, &r) && r.sampler == 3 && r.origin_input == 4 && r.light_register == 17 &&
                    !r.toward_light && r.colour_register == 18 && r.ambient_registers[0] == 37 &&
                    r.ambient_registers[1] == 38 && r.up[2] == 1 && r.diffuse_wrap == 0.25f &&
                    r.output_scale == 0.1f && r.max_distance == 100;
  gv::Ctab c;
  const bool inputs = gv::ReadCtab(f.tokens, &c) && c.eye == 15 && c.origin == 4 && c.normal == 1 &&
                      Reads(f.tokens, 2, 15) && Reads(f.tokens, 1, 1);
  const bool shape = dcls0 == 0 && reads0 == 0 && texldl0 == 1 && dcls == 1 && reads == 1 && texldl == 1 &&
                     temps == temps0 + 2 && t.size() == f.tokens.size() + 39 && gv::IsPatchedVehglassShader(t) &&
                     !gv::PatchVehglassLookup(again) && TimesTen(f.tokens, env);
  recomp::hlsl::Program prog;
  std::string err;
  const bool translated = TranslateAsTraced(t, r, &prog, &err);
  if (!translated) std::printf("  %s ps%d: %s\n", folder, f.index, err.c_str());
  const bool traced = translated && Traced(prog, 4) && prog.stage == recomp::hlsl::Stage::Pixel;
  if (!shape || !ctab || !inputs || !traced)
    std::printf("  %s ps%d: s3 declared %d read %d, texldl %d, temps %u -> %u, %zu tokens -> %zu, ctab %d, "
                "inputs %d, times ten %d, traced %d\n",
                folder, f.index, dcls, reads, texldl, temps0, temps, f.tokens.size(), t.size(), ctab, inputs,
                TimesTen(f.tokens, env), traced);
  std::printf("  %s ps%d: %zu tokens -> %zu, the temps r%u and r%u\n", folder, f.index, f.tokens.size(), t.size(),
              temps0, temps0 + 1);
  return shape && ctab && inputs && traced;
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
    bool ok = true, glass = false;
    int effects = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".fxc") continue;
      std::ifstream in(e.path(), std::ios::binary);
      const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::vector<Fragment> frags;
      if (!Fragments(data, &frags)) continue;
      effects++;
      const std::string name = e.path().stem().string();
      glass = glass || name == "gta_vehicle_vehglass";
      for (const Fragment& f : frags) {
        Tokens t = f.tokens;
        if (!gv::PatchVehglassLookup(t)) {
          if (gv::IsPatchedVehglassShader(f.tokens)) ok = false;
          continue;
        }
        patched.insert({name, f.pixel ? f.index : -1 - f.index});
        if (name == "gta_vehicle_vehglass" && f.pixel) ok = GlassChecks(folder, f, t) && ok;
      }
    }
    const std::set<std::pair<std::string, int>> want = {
        {"gta_vehicle_vehglass", 2}, {"gta_vehicle_vehglass", 3}, {"gta_vehicle_vehglass", 4}};
    for (const auto& [name, index] : patched)
      if (!want.count({name, index})) std::printf("  %s: %s shader %d patched too\n", folder, name.c_str(), index);
    std::printf("  %s: %d effects offered\n", folder, effects);
    Check(ok && glass && patched == want,
          (std::string(folder) + ": of every effect, gta_vehicle_vehglass's pixel shaders 2-4 only patched (not "
                                 "gta_vehicle_lightsemissive's), each once (R from c15, v4 and v1, one read of s3 "
                                 "into free temps, the texldl kept), the consumer's answer, the term times 10, "
                                 "translated with the ray query's call")
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
  std::puts("vehicle glass patch: ok");
  return 0;
}
