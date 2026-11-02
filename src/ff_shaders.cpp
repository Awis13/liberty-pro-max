// The shader constants FusionFix's shaders read (its shaders.ixx, by its
// idea; GPL-3.0, its code not taken): registers above the game's own,
// written through the game's Direct3D device at the start of each frame's
// drawing (grcSetup::BeginDraw) and whenever a viewport becomes current.
// Only with FusionFix's shaders (GTAIV_FF_SHADERS, update_folder.cpp):
//   PS c209, VS c227: the viewport's clip planes for the logarithmic depth
//     (no z-fighting far away);
//   PS c217, c218: shadow filter, cascade blend, tone mapping;
//   PS c220: water reflection's half-pixel offset (by the water quality
//     setting), light clamping;
//   PS c221: definition, snow, tree alpha;
//   PS c222: auto exposure, smooth shorelines, motion blur scaled by the
//     frame time (the same at any frame rate);
//   PS c223: tree lighting, field of view, definition, night;
//   PS c208, c210, c211, VS c235-c237: sun shafts' intensity, volumetric
//     fog (on with GTAIV_FF_VOLUMETRIC_FOG), smooth light volumes, the
//     sun's light for the rain: FusionFix's timecycext.dat and the game's
//     timecyc.dat blended by hour and weather (wf2_fog2.cpp);
//   VS c233: the trees' wind sway.
//   PS c212-c216, VS c228-c234: the sky's colours and sun, as the game
//     last set them on gta_atmoscatt_clouds.fxc (a frame late).
// FusionFix's settings behind them, each an option at FusionFix's default:
//   GTAIV_MOTION_BLUR=0-4: off, 1/8, 1/4 (default), 1/2, full;
//   GTAIV_FF_SHADOW_FILTER=sharp|soft|chss (or 0-2; sharp by default):
//     contact hardening (CHSS) only at Very High shadow quality;
//   GTAIV_FF_TREE_LIGHTING=pc|pc+|console (or 0-2; PC+ by default);
//   GTAIV_FF_TREE_ALPHA=pc|pc+|console (or 0-2; PC by default);
//   GTAIV_FF_TONE_MAPPING (off by default): highlights compressed, not
//     clipped;
//   GTAIV_FF_NO_BLOOM_COLOR_SHIFT: with tone mapping, bloom keeps its
//     colour;
//   GTAIV_FF_AUTO_EXPOSURE: exposure adapts to light as on the consoles;
//   GTAIV_FF_DEFINITION: the game's Definition option, a sharper image
//     (0: softer);
//   GTAIV_FF_UNCLAMP_LIGHTING (off by default): direct sunlight not
//     clamped (it reaches further);
//   GTAIV_FF_SMOOTH_SHORELINES: soft edges where water meets land;
//   GTAIV_FF_SMOOTH_LIGHT_VOLUMES: soft edges where light volumes meet
//     geometry;
//   GTAIV_FF_CASCADE_SHADOWS: the cascaded shadows as FusionFix's shaders
//     draw them: the atlas R32F (not R16F, 00AC4C33), the deferred
//     G-buffers A8R8G8B8 (not A2R10G10B10, 00A95D21), the cascades'
//     ranges 10/30/85/256 m at every quality and the first set of their
//     per-cascade offsets zeroed (the tables at 00F3E1A8, 00F3E210,
//     00F3E288 that 008C80B0 blends from), and each cascade rendered at
//     its atlas tile's size, not twice it (008C896D);
//   GTAIV_FF_HIGH_RES_SHADOWS (off by default; with the cascade fixes
//     only): the shadow atlas twice the size each way (the cascades
//     rendered at twice the tile size, as the game does), CHSS's largest
//     softness and light size (PS c217.xy) doubled to match.
// CHSS's light size (PS c217.y) is timecycext.dat's too (500 without it).

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

bool FfShadersOn();  // update_folder.cpp

// FusionFix's timecycext values blended for the frame (wf2_fog2.cpp).
struct FfTimecycExt {
  float fog[4];
  float dir_light[4];
  float sun_shafts;
  float chss_light_size;
};
void FfTimecycExtNow(FfTimecycExt& out);
bool FfVolumetricFog();

namespace {

constexpr uint32_t kDevice = 0x01745630;     // rage::grcDevice: the IDirect3DDevice9
constexpr uint32_t kViewport = 0x018CADA4;   // rage: the current grcViewport
constexpr uint32_t kWorld = 0x00F488EC;      // non-zero once the world is set up
constexpr uint32_t kLoading = 0x018CB06C;    // CMenuManager: a loading screen is up
constexpr uint32_t kTimeStep = 0x010FCB74;   // CTimer::ms_fTimeStep
constexpr uint32_t kTimeScale = 0x00F2E090;  // CTimer::ms_fTimeScale
constexpr uint32_t kWindAngle = 0x012234B4;  // CTreeImposters::ms_windAng
constexpr uint32_t kHours = 0x01151694, kMinutes = 0x01151690;  // CClock
// The menu settings' values by their ids (the table of names at 00E58460:
// PREF_SHADOW_QUALITY 153, PREF_WATER_QUALITY 155).
constexpr uint32_t kShadowQuality = 0x010FC31C;  // 0-4, 4 Very High
constexpr uint32_t kWaterQuality = 0x010FC324;   // 0-3

// One of a few values: its number or its name (any case), else the default.
int Choice(const char* env, int def, std::initializer_list<std::string_view> names) {
  const char* e = std::getenv(env);
  if (!e || !*e) return def;
  const std::string_view v(e);
  if (v.size() == 1 && v[0] >= '0' && size_t(v[0] - '0') < names.size()) return v[0] - '0';
  int i = 0;
  for (std::string_view n : names) {
    if (n.size() == v.size() && std::equal(n.begin(), n.end(), v.begin(), [](char a, char b) {
          return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b));
        }))
      return i;
    i++;
  }
  return def;
}

struct Settings {
  int shadow_filter;  // 0 sharp, 1 soft, 2 contact hardening (CHSS)
  int tree_lighting;  // 0 PC, 1 PC+, 2 console
  int tree_alpha;     // 0 PC, 1 PC+, 2 console
  bool tone_mapping, no_bloom_shift, auto_exposure, definition, unclamp, shorelines, light_volumes;
};

const Settings& Prefs() {
  static const Settings s = {
      Choice("GTAIV_FF_SHADOW_FILTER", 0, {"sharp", "soft", "chss"}),
      Choice("GTAIV_FF_TREE_LIGHTING", 1, {"pc", "pc+", "console"}),
      Choice("GTAIV_FF_TREE_ALPHA", 0, {"pc", "pc+", "console"}),
      OptionOn("GTAIV_FF_TONE_MAPPING", false),
      OptionOn("GTAIV_FF_NO_BLOOM_COLOR_SHIFT", true),
      OptionOn("GTAIV_FF_AUTO_EXPOSURE", true),
      OptionOn("GTAIV_FF_DEFINITION", true),
      OptionOn("GTAIV_FF_UNCLAMP_LIGHTING", false),
      OptionOn("GTAIV_FF_SMOOTH_SHORELINES", true),
      OptionOn("GTAIV_FF_SMOOTH_LIGHT_VOLUMES", true),
  };
  return s;
}

bool CascadeShadows() {
  static const bool on = FfShadersOn() && OptionOn("GTAIV_FF_CASCADE_SHADOWS", true);
  return on;
}

bool HighResShadows() {
  static const bool on = CascadeShadows() && OptionOn("GTAIV_FF_HIGH_RES_SHADOWS", false);
  return on;
}

float F32(uint32_t va) {
  const uint32_t b = Rd32(va);
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}

// IDirect3DDevice9::SetVertexShaderConstantF (94) / SetPixelShaderConstantF
// (109) for one register, the values put below the stack.
void SetConstant(Ctx& c, bool pixel, uint32_t reg, const float (&v)[4]) {
  const uint32_t dev = Rd32(kDevice);
  if (!dev) return;
  const uint32_t fn = Rd32(Rd32(dev) + 4 * (pixel ? 109 : 94));
  c.esp -= 16;
  std::memcpy(GPtr(c.esp), v, 16);
  CallGuest(c, fn, {dev, reg, c.esp, 1});
  c.esp += 16;
}

// The calls run in the middle of the game's code: all its registers
// (hook::KeepRegs) are given back after them.
using Saved = hook::KeepRegs;

void SkyConstants(Ctx& c);

void FrameConstants(Ctx& c) {
  if (!Rd32(kWorld) || Rd8(kLoading)) return;
  Saved keep(c);
  const float wind[4] = {F32(kWindAngle), std::clamp(F32(kTimeScale) * 0.015f, 0.0015f, 0.015f), 0, 0};
  SetConstant(c, false, 233, wind);
  const Settings& s = Prefs();
  FfTimecycExt ext;
  FfTimecycExtNow(ext);
  // The filter's softness and bias (sharp, soft, CHSS), CHSS itself (at
  // Very High shadow quality only), the cascades' blend.
  static constexpr float kSoftness[3] = {1.5f, 3.0f, 1.5f}, kBias[3] = {5.0f, 8.0f, 5.0f};
  const bool chss = s.shadow_filter == 2 && int32_t(Rd32(kShadowQuality)) >= 4;
  const float shadows[4] = {kSoftness[s.shadow_filter], kBias[s.shadow_filter], chss ? 1.0f : 0.0f, 0.1f};
  SetConstant(c, true, 218, shadows);
  // CHSS's largest softness and light size (in the atlas's texels: twice
  // with the high resolution atlas); tone mapping, and bloom without its
  // colour shift (with tone mapping only).
  const float tone = s.tone_mapping ? 1.0f : 0.0f;
  const float texels = HighResShadows() ? 2.0f : 1.0f;
  const float chss_tone[4] = {20.0f * texels, ext.chss_light_size * texels, tone,
                              s.tone_mapping && s.no_bloom_shift ? 1.0f : 0.0f};
  SetConstant(c, true, 217, chss_tone);
  // The water reflection's half-pixel by the water quality setting (its
  // 160x64 base doubling per step); light clamped to 4/3 x - 1/3 unless
  // unclamped.
  const int32_t wq = int32_t(Rd32(kWaterQuality));
  const int q = wq >= 0 && wq < 3 ? wq : 3;
  const float water[4] = {0.5f / float(160 << q), 0.5f / float(64 << q), s.unclamp ? 1.0f : 4.0f / 3.0f,
                          s.unclamp ? 0.0f : -1.0f / 3.0f};
  SetConstant(c, true, 220, water);
  // Definition, snow, menu blur, the trees' alpha scale (PC, PC+, console).
  static constexpr float kTreeAlpha[3] = {0.625f, 1.0f, 4.0f};
  const float definition = s.definition ? 1.0f : 0.0f;
  const float settings[4] = {definition, 0.0f, 0.0f, kTreeAlpha[s.tree_alpha]};
  SetConstant(c, true, 221, settings);
  const float step = std::max(F32(kTimeStep), 1e-4f);
  // GTAIV_MOTION_BLUR=0-4: off, 1/8, 1/4 (FusionFix's default), 1/2, full.
  static const float blur = [] {
    const char* e = std::getenv("GTAIV_MOTION_BLUR");
    const int level = e && *e >= '0' && *e <= '4' ? *e - '0' : 2;
    return level == 0 ? 0.0f : 1.0f / float(1 << (4 - level));
  }();
  // Eye adaptation's rate (1/9 as on the consoles, else 1/16), smooth shorelines,
  // motion blur per frame time.
  const float post[4] = {s.auto_exposure ? 1.0f / 9.0f : 1.0f / 16.0f, s.shorelines ? 1.0f : 0.0f, 0.0f,
                         blur / (30.0f * step)};
  SetConstant(c, true, 222, post);
  const uint32_t viewport = Rd32(kViewport);
  const float fov = viewport ? F32(viewport + 0x2B8) : 45.0f;
  const uint32_t h = Rd32(kHours) & 0xFF, m = Rd32(kMinutes) & 0xFF;
  const bool day = ((h == 6 && m >= 45) || h > 6) && ((h == 19 && m < 15) || h < 19);
  // Tree lighting (PC 0, PC+ 1, console 2), field of view, definition, night.
  const float view[4] = {float(s.tree_lighting), fov > 45.0f ? std::pow(fov / 45.0f, 1.25f) : 1.0f, definition,
                         day ? 0.0f : 1.0f};
  SetConstant(c, true, 223, view);
  // The fog's density, height falloff, power, altitude tweak.
  SetConstant(c, true, 211, ext.fog);
  SetConstant(c, false, 235, ext.fog);
  // PQ peak, volumetric fog, smooth light volumes.
  const float fog2[4] = {1.0f / 100.0f, FfVolumetricFog() ? 1.0f : 0.0f, s.light_volumes ? 1.0f : 0.0f, 0.0f};
  SetConstant(c, true, 210, fog2);
  SetConstant(c, false, 236, fog2);
  // The sun's light colour and multiplier (the rain particles').
  SetConstant(c, false, 237, ext.dir_light);
  // Sun shafts: intensity, density, decay.
  const float shafts[4] = {ext.sun_shafts, 0.9f, 0.95f, 0.0f};
  SetConstant(c, true, 208, shafts);
  SkyConstants(c);
}

// The sky shader's parameters, as the game sets them (grmShaderInfo::
// SetParam, 0040B280, thiscall: ?, index, data, size, ?, ?; ret 18h): the
// shader's name at +20, its parameters' count at +0C; a parameter's index
// by name from 0040AE60 (thiscall: name, 1).
constexpr const char* kSkyParams[] = {"AzimuthColor", "AzimuthHeight", "AzimuthColorEast", "AzimuthStrength",
                                      "SkyColor",     "HDRExposure",   "SunColor",         "HDRSunExposure",
                                      "SunDirection"};
constexpr int kSkyCount = 9;
std::mutex g_sky_mu;
float g_sky[kSkyCount][4];
std::unordered_map<uint32_t, std::array<int32_t, kSkyCount>> g_sky_index;  // by grmShaderInfo

void SkyConstants(Ctx& c) {
  float v[kSkyCount][4];
  {
    std::lock_guard<std::mutex> l(g_sky_mu);
    std::memcpy(v, g_sky, sizeof v);
  }
  const float azimuth[4] = {v[0][0], v[0][1], v[0][2], v[1][0]};
  const float east[4] = {v[2][0], v[2][1], v[2][2], v[3][0]};
  const float sky[4] = {v[4][0], v[4][1], v[4][2], v[5][0]};
  const float sun[4] = {v[6][0], v[6][1], v[6][2], v[7][0]};
  const float dir[4] = {v[8][0], -v[8][2], v[8][1], 0.0f};
  SetConstant(c, true, 212, azimuth);
  SetConstant(c, false, 228, azimuth);
  SetConstant(c, true, 213, east);
  SetConstant(c, false, 229, east);
  SetConstant(c, true, 214, sky);
  SetConstant(c, false, 234, sky);
  SetConstant(c, true, 215, sun);
  SetConstant(c, false, 231, sun);
  SetConstant(c, true, 216, dir);
  SetConstant(c, false, 232, dir);
}

// The cascades' tables that 008C80B0 blends between each frame (by the
// shadow quality's row, 0-3): the split distances near (00F3E1A8) and far
// (00F3E210), 5 floats a row, the first (0.1) kept; the per-cascade offsets
// near (00F3E288, 4 floats a row), zeroed. The game's own were made for its
// shaders (ranges up to 175 m).
void CascadeTables() {
  static std::once_flag once;
  std::call_once(once, [] {
    static constexpr float kRange[4] = {10.0f, 30.0f, 85.0f, 256.0f};
    for (uint32_t row = 0; row < 4; row++) {
      for (uint32_t i = 0; i < 4; i++) {
        hook::WrF32(0x00F3E1A8 + row * 20 + (i + 1) * 4, kRange[i]);
        hook::WrF32(0x00F3E210 + row * 20 + (i + 1) * 4, kRange[i]);
        hook::WrF32(0x00F3E288 + row * 16 + i * 4, 0.0f);
      }
    }
  });
}

}  // namespace

void f_0040B280_lifted(Ctx& c);
void f_0040B280(Ctx& c) {
  if (FfShadersOn()) {
    const uint32_t info = c.ecx, index = Rd32(c.esp + 8), data = Rd32(c.esp + 12);
    const uint32_t path = Rd32(info + 0x20);
    if (path && data && (Rd32(info + 0x0C) & 0xFFFF)) {
      std::string_view name(reinterpret_cast<const char*>(GPtr(path)));
      name = name.substr(name.find_last_of("/\\") + 1);
      if (name == "gta_atmoscatt_clouds.fxc") {
        std::array<int32_t, kSkyCount>* idx;
        {
          std::lock_guard<std::mutex> l(g_sky_mu);
          auto it = g_sky_index.find(info);
          idx = it == g_sky_index.end() ? nullptr : &it->second;
        }
        if (!idx) {
          std::array<int32_t, kSkyCount> found;
          Saved keep(c);
          for (int i = 0; i < kSkyCount; i++) {
            const uint32_t len = uint32_t(std::strlen(kSkyParams[i])) + 1;
            c.esp -= 32;
            std::memcpy(GPtr(c.esp), kSkyParams[i], len);
            c.ecx = info;
            found[i] = int32_t(CallGuest(c, 0x0040AE60, {c.esp, 1}));
            c.esp += 32;
          }
          c.ecx = info;
          std::lock_guard<std::mutex> l(g_sky_mu);
          idx = &(g_sky_index[info] = found);
        }
        std::lock_guard<std::mutex> l(g_sky_mu);
        for (int i = 0; i < kSkyCount; i++)
          if ((*idx)[i] > 0 && uint32_t((*idx)[i]) == index) std::memcpy(g_sky[i], GPtr(data), 16);
      }
    }
  }
  f_0040B280_lifted(c);
}

// grcSetup::BeginDraw: cmp byte [esp+8], 0 (then je).
void hook_005B62A8(Ctx& c) {
  if (CascadeShadows()) CascadeTables();
  if (FfShadersOn()) FrameConstants(c);
  lift::U<1> v = Rd8(c.esp + 8);
  Flags f;
  UnpackEFlags(f, c.eflags);
  lift::Sub<1>(f, v, 0);
  c.eflags = PackEFlags(f);
}

// A viewport (esi) made current: call 414050 (its transforms; esi already
// pushed). The logarithmic depth's constants from its clip planes first.
void hook_004142BE(Ctx& c) {
  if (FfShadersOn()) {
    Saved keep(c);
    const uint32_t vp = Rd32(kViewport);
    if (vp) {
      const float n = F32(vp + 0x2C0), f = F32(vp + 0x2C4);
      if (n > 0 && f > n) {
        const float depth[4] = {1.0f / n, 1.0f / std::log2(f / n), f / n, n};
        SetConstant(c, false, 227, depth);
        SetConstant(c, true, 209, depth);
      }
    }
  }
  CallGuest(c, 0x00414050, {c.esi});
}

// The shadow atlas's format, when the device renders to R16F: mov dword
// [014FE20C], 3 (rage's R16F). FusionFix's shaders read it as R32F (4),
// the format the game takes when R16F is missing.
void hook_00AC4C33(Ctx&) {
  Wr32(0x014FE20C, CascadeShadows() ? 4 : 3);
}

// The deferred G-buffers' format when the device has A2R10G10B10: mov
// dword [esp+40h], 5. FusionFix's shaders are made for A8R8G8B8 (2), the
// format the game takes without it.
void hook_00A95D21(Ctx& c) {
  Wr32(c.esp + 0x40, CascadeShadows() ? 2 : 5);
}

// The cascaded shadows' targets (008C8910, cdecl: size 128 << shadow
// quality up to 2048, ?, the atlas's format): the atlas 4 x size by size
// ([012284A0], [012284A4]), CASCADE_TARGET_D24 and _R32, where each cascade
// is drawn before 008C8B10 copies it into its quarter of the atlas (the
// quarter from the atlas's own width), twice the size each way: add esi,
// esi. FusionFix's shaders take the cascade at the tile's size, so no
// doubling, unless the atlas is doubled too (high resolution shadows).
void hook_008C896D(Ctx& c) {
  if (CascadeShadows() && !HighResShadows()) return;
  Flags f;
  UnpackEFlags(f, c.eflags);
  c.esi = lift::Add<4>(f, c.esi, c.esi);
  c.eflags = PackEFlags(f);
}

// The atlas's own creation: mov eax, [012284A4]; push 20h; push eax; mov
// eax, [012284A0]; push eax (width, height, then the device's call with
// the format edi). With high resolution shadows an R32F atlas (4) of the
// usual shape is made twice as large each way; the sizes the game keeps
// (and its texel constants from them) stay as they are.
void hook_008C89FB(Ctx& c) {
  uint32_t w = Rd32(0x012284A0), h = Rd32(0x012284A4);
  c.eax = w;
  if (HighResShadows() && c.edi == 4 && h >= 256 && w == 4 * h) {
    w *= 2;
    h *= 2;
  }
  hook::Push(c, 0x20);
  hook::Push(c, h);
  hook::Push(c, w);
}

}  // namespace recomp::gen
