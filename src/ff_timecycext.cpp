// FusionFix's timecycle extension and volumetric fog (#125) on the game's
// side, by their ideas (FusionFix's timecycext.ixx, timecyc.ixx and
// settings.ixx; GPL-3.0, its code not taken). Only with FusionFix's
// shaders (GTAIV_FF_SHADERS): ff_shaders.cpp writes what is blended here
// into their constants (PS c208, c210, c211, c217.y; VS c235-c237).
//   - pc\data\timecycext.dat (a mod pack's file, from the mods folder or
//     the update folder; built-in defaults when there is none): per
//     weather (9) and timecycle hour (11) the fog's density, height
//     falloff, altitude tweak and power, the sun shafts' intensity and the
//     contact hardening shadows' light size; missing (-1) values filled in
//     between the hours around them;
//   - pc\data\timecyclemodifiersext.dat: the same per timecycle modifier
//     (by its name's hash, the game's own 0045CF50), each blended in by
//     the strength the game applies the modifier with (00A36790; an
//     interior's through the room blend 00A35CA0 and its fade 00F498E0);
//   - both blended at each clock update (CClock::Update, 007F8AA0) by the
//     timecycle's own hour (its keyframe hours at 00F49904, the clock's
//     overrides) and weather (old 01311D88, new 01311D3C, blend 01311D70),
//     the way the game blends its keyframes (00A39420);
//   - the sun's directional light (colour and multiplier) blended the same
//     from the game's own timecyc.dat keyframes (013F1150, [hour][weather]
//     of 210h bytes, colour at +8, multiplier at +0C): the light FusionFix's
//     rain particles take (VS c237). A keyframe with no sun (black,
//     multiplier below 1) takes visualsettings' rain default light
//     (00F4B0D0) by the share it misses, as FusionFix has it.
// Option:
//   GTAIV_FF_VOLUMETRIC_FOG (off by default, as FusionFix): the fog drawn
//     by FusionFix's shaders (PS c210.y), and the game's own distance fog
//     moved out of its way: every timecyc.dat far clip 4500 m (00A3BB7B),
//     the timecycle modifiers' own min and max far clips none (00A356AC,
//     00A356BD; the two names FusionFix spares have none in the game's
//     data, or are not in it), the cutscenes' far clip keyframes none
//     (007F5F02), and the camera height's far clip and near fog
//     multipliers (visualsettings misc.Multiplier.*, 2 and 0.5 at 250 m
//     up; 00A3A3FB, 00A3A417) kept at 1.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hook_util.h"
#include "option.h"
#include "runtime/hle/kernel32_state.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

bool FfShadersOn();  // update_folder.cpp

// What ff_shaders.cpp writes: PS c211 / VS c235 (fog), VS c237 (the sun's
// light), PS c208.x (sun shafts), PS c217.y (CHSS light size).
struct FfTimecycExt {
  float fog[4];
  float dir_light[4];
  float sun_shafts;
  float chss_light_size;
};

namespace {

constexpr int kWeathers = 9, kHours = 11, kParams = 6;
// The parameters' order in the files and their defaults (FusionFix's).
enum { kDensity, kFalloff, kAltitude, kPower, kShafts, kChss };
constexpr float kDefaults[kParams] = {0.004f, 0.015f, 0.7f, 1.0f, 0.002f, 500.0f};

constexpr uint32_t kHourTable = 0x00F49904;  // the keyframes' hours (bytes 0, 5, ... 22, 24)
constexpr uint32_t kKeyframes = 0x013F1150;  // timecyc.dat: [hour][weather], 210h each
constexpr uint32_t kKeyframeSize = 0x210;
constexpr uint32_t kOldWeather = 0x01311D88, kNewWeather = 0x01311D3C, kWeatherBlend = 0x01311D70;
constexpr uint32_t kClockHours = 0x01151694, kClockMinutes = 0x01151690, kClockSeconds = 0x011516A0;
constexpr uint32_t kHoursOverride = 0x0115169C, kMinutesOverride = 0x01151684;  // -1: none
constexpr uint32_t kRainLight = 0x00F4B0D0;  // rain.defaultlight.red, green, blue, alpha
constexpr uint32_t kModifiers = 0x013E67D0, kModifierSize = 0xBC, kModifierCount = 225;
constexpr uint32_t kInteriorFade = 0x00F498E0;  // the interior modifiers' blend

bool VolumetricFog() {
  static const bool on = FfShadersOn() && OptionOn("GTAIV_FF_VOLUMETRIC_FOG", false);
  return on;
}

// The game's name hash (0045CF50, seed 0): lower case, '\' as '/', one
// leading quote skipped and the name ending at the next.
uint32_t NameHash(const std::string& s) {
  size_t i = 0;
  const bool quoted = !s.empty() && s[0] == '"';
  if (quoted) i++;
  uint32_t h = 0;
  for (; i < s.size() && s[i]; i++) {
    char ch = s[i];
    if (quoted && ch == '"') break;
    if (ch >= 'A' && ch <= 'Z')
      ch = char(ch + 32);
    else if (ch == '\\')
      ch = '/';
    h += uint32_t(int32_t(int8_t(ch)));
    h *= 0x401;
    h ^= h >> 6;
  }
  h *= 9;
  h ^= h >> 11;
  h *= 0x8001;
  return h;
}

// A file of pc\data on the host, found as the game finds its own: a mod's
// first, then the update folder's, then the install's.
std::string DataFile(const char* name) {
  return HostPath(Game().guest_dir + "\\pc\\data\\" + name);
}

// The lines that carry values: not empty, not a comment ('/' or '#').
std::vector<std::string> DataLines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream f(path, std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    const size_t at = line.find_first_not_of(" \t\r");
    if (at == std::string::npos || line[at] == '/' || line[at] == '#') continue;
    lines.push_back(line.substr(at));
  }
  return lines;
}

// Up to n floats from s (from its start), the rest left as they are.
void Floats(const char* s, float* out, int n) {
  for (int i = 0; i < n; i++) {
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s) break;
    out[i] = v;
    s = end;
  }
}

struct Tables {
  float v[kHours][kWeathers][kParams];
  std::unordered_map<uint32_t, std::array<float, kParams>> modifiers;  // by name hash; -1: none
};

// Missing (-1) values of one weather's parameter filled in along the
// hours, round the clock, between the given ones around them.
void FillGaps(Tables& t, int w, int p) {
  int start = -1;
  for (int h = 0; h < kHours; h++)
    if (t.v[h][w][p] != -1.0f) {
      start = h;
      break;
    }
  if (start < 0) {
    for (int h = 0; h < kHours; h++) t.v[h][w][p] = kDefaults[p];
    return;
  }
  int a = start;
  do {
    int b = (a + 1) % kHours;
    while (t.v[b][w][p] == -1.0f) b = (b + 1) % kHours;
    const int n = (b - a + kHours) % kHours == 0 ? kHours : (b - a + kHours) % kHours;
    const float va = t.v[a][w][p], vb = t.v[b][w][p];
    for (int i = 1; i < n; i++) {
      const float f = float(i) / float(n);
      t.v[(a + i) % kHours][w][p] = va * (1.0f - f) + vb * f;
    }
    a = b;
  } while (a != start);
}

Tables* LoadTables() {
  auto* t = new Tables;
  for (auto& h : t->v)
    for (auto& w : h) std::copy(std::begin(kDefaults), std::end(kDefaults), w);
  // One line per hour, the weathers one after another.
  const std::vector<std::string> lines = DataLines(DataFile("timecycext.dat"));
  size_t next = 0;
  for (int w = 0; w < kWeathers; w++)
    for (int h = 0; h < kHours && next < lines.size(); h++) Floats(lines[next++].c_str(), t->v[h][w], kParams);
  for (int w = 0; w < kWeathers; w++)
    for (int p = 0; p < kParams; p++) FillGaps(*t, w, p);
  // A modifier's name, then its values.
  for (const std::string& line : DataLines(DataFile("timecyclemodifiersext.dat"))) {
    std::istringstream in(line);
    std::string name;
    in >> name;
    if (name.empty()) continue;
    std::array<float, kParams> v;
    v.fill(-1.0f);
    Floats(line.c_str() + std::min(line.size(), line.find(name) + name.size()), v.data(), kParams);
    t->modifiers[NameHash(name)] = v;
  }
  return t;
}

std::mutex g_mu;
std::vector<std::pair<uint32_t, float>> g_applied;  // modifier index, strength, this frame
int32_t g_room = -1;                                // the interior modifier being blended
bool g_room_last = false;                           // the last one applied was an interior's
FfTimecycExt g_now = {{kDefaults[kDensity], kDefaults[kFalloff], kDefaults[kPower], kDefaults[kAltitude]},
                      {0, 0, 0, 0},
                      kDefaults[kShafts],
                      kDefaults[kChss]};

// The modifier at va (an entry of the game's table), else -1.
int32_t ModifierIndex(uint32_t va) {
  if (va < kModifiers || va >= kModifiers + kModifierCount * kModifierSize) return -1;
  if ((va - kModifiers) % kModifierSize || !Rd32(va)) return -1;
  return int32_t((va - kModifiers) / kModifierSize);
}

// A table modifier applied with its strength (none at 0 or below: the
// game skips it then). A room left out is not the last room counted
// either; anything else left out (the rooms' blend itself, built on the
// stack) keeps the last room for the fade after it.
void Applied(int32_t index, float strength, bool room) {
  if (index < 0 || !(strength > 0.0f)) {
    if (room) {
      std::lock_guard<std::mutex> l(g_mu);
      g_room_last = false;
    }
    return;
  }
  std::lock_guard<std::mutex> l(g_mu);
  if (g_applied.size() < 1024) g_applied.emplace_back(uint32_t(index), strength);
  g_room_last = room;
}

// The sun's light of one keyframe: colour 0-1 and multiplier.
std::array<float, 4> SunLight(uint32_t hour, uint32_t weather) {
  const uint32_t kf = kKeyframes + (hour * kWeathers + weather) * kKeyframeSize;
  const uint32_t rgb = Rd32(kf + 0x08);
  std::array<float, 4> v = {float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                            float(rgb & 0xFF) / 255.0f, hook::RdF32(kf + 0x0C)};
  if (v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f && v[3] < 1.0f) {
    const float missing = 1.0f - v[3];
    for (int i = 0; i < 4; i++) v[i] = hook::RdF32(kRainLight + 4 * i) * missing;
  }
  return v;
}

// CClock::Update: the values for this frame from the hour, the weather and
// the modifiers applied since the last.
void Update() {
  static Tables* const t = LoadTables();
  // The timecycle's hour (00A39420): the clock's, or its override.
  const int32_t ho = int32_t(Rd32(kHoursOverride)), mo = int32_t(Rd32(kMinutesOverride));
  const int32_t hours = ho != -1 ? ho : int32_t(Rd32(kClockHours));
  const int32_t minutes = mo != -1 ? mo : int32_t(Rd32(kClockMinutes));
  float time = float(int32_t(Rd32(kClockSeconds))) / 3600.0f + float(minutes) / 60.0f + float(hours);
  time = std::min(time, 23.999f);
  uint8_t table[kHours + 1];
  for (int i = 0; i <= kHours; i++) table[i] = Rd8(kHourTable + i);
  int h1 = 0;
  while (h1 < kHours - 1 && time >= float(table[h1 + 1])) h1++;
  const int h2 = (h1 + 1) % kHours;
  const float span = float(table[h1 + 1]) - float(table[h1]);
  const float ft = span > 0 ? std::clamp((time - float(table[h1])) / span, 0.0f, 1.0f) : 0.0f;
  uint32_t w_old = Rd32(kOldWeather), w_new = Rd32(kNewWeather);
  if (w_old >= uint32_t(kWeathers)) w_old = 0;
  if (w_new >= uint32_t(kWeathers)) w_new = 0;
  const float fw = std::clamp(hook::RdF32(kWeatherBlend), 0.0f, 1.0f);
  const float c0 = (1 - ft) * (1 - fw), c1 = ft * (1 - fw), c2 = (1 - ft) * fw, c3 = ft * fw;
  float v[kParams];
  for (int p = 0; p < kParams; p++)
    v[p] = t->v[h1][w_old][p] * c0 + t->v[h2][w_old][p] * c1 + t->v[h1][w_new][p] * c2 + t->v[h2][w_new][p] * c3;
  std::vector<std::pair<uint32_t, float>> applied;
  {
    std::lock_guard<std::mutex> l(g_mu);
    applied.swap(g_applied);
    g_room_last = false;
  }
  for (const auto& [index, strength] : applied) {
    const auto it = t->modifiers.find(Rd32(kModifiers + index * kModifierSize));
    if (it == t->modifiers.end()) continue;
    for (int p = 0; p < kParams; p++)
      if (it->second[p] != -1.0f) v[p] = v[p] * (1.0f - strength) + it->second[p] * strength;
  }
  const std::array<float, 4> s00 = SunLight(h1, w_old), s10 = SunLight(h2, w_old), s01 = SunLight(h1, w_new),
                             s11 = SunLight(h2, w_new);
  FfTimecycExt now;
  for (int i = 0; i < 4; i++) now.dir_light[i] = s00[i] * c0 + s10[i] * c1 + s01[i] * c2 + s11[i] * c3;
  now.fog[0] = std::max(v[kDensity], 0.0f);
  now.fog[1] = std::max(v[kFalloff], 1e-7f);
  now.fog[2] = std::max(v[kPower], 0.0f);
  now.fog[3] = std::clamp(v[kAltitude], 0.0f, 1.0f);
  now.sun_shafts = std::max(v[kShafts], 0.0f);
  now.chss_light_size = v[kChss];
  std::lock_guard<std::mutex> l(g_mu);
  g_now = now;
}

}  // namespace

// For ff_shaders.cpp (any thread).
void FfTimecycExtNow(FfTimecycExt& out) {
  std::lock_guard<std::mutex> l(g_mu);
  out = g_now;
}
bool FfVolumetricFog() { return VolumetricFog(); }

// CClock::Update: FusionFix's values blended for the frame first.
void f_007F8AA0_lifted(Ctx& c);
void f_007F8AA0(Ctx& c) {
  if (FfShadersOn()) Update();
  f_007F8AA0_lifted(c);
}

// CTimeCycle: a modifier applied to a keyframe (thiscall: modifier,
// strength, flag; ret 0Ch). Its strength kept when it is one of the
// table's (not an interior's blend, built on the stack).
void f_00A36790_lifted(Ctx& c);
void f_00A36790(Ctx& c) {
  if (FfShadersOn()) {
    Applied(ModifierIndex(Rd32(c.esp + 4)), hook::RdF32(c.esp + 8), false);
  }
  f_00A36790_lifted(c);
}

// An interior's modifier blended into the room blend (00A399B0): call
// 00A35CA0 (thiscall: modifier, strength; ret 8, arguments pushed), first
// the room's own modifier from the table ...
void hook_00A39DE6(Ctx& c) {
  if (FfShadersOn()) {
    const int32_t index = ModifierIndex(Rd32(c.esp));
    std::lock_guard<std::mutex> l(g_mu);
    g_room = index;
  }
  CallGuest(c, 0x00A35CA0, {});
  c.esp += 8;  // its ret 8
}

// ... then that into the rooms' blend, by the room's share: the room's
// modifier counted with it.
void hook_00A39DFD(Ctx& c) {
  if (FfShadersOn()) {
    int32_t index;
    {
      std::lock_guard<std::mutex> l(g_mu);
      index = g_room;
    }
    Applied(index, hook::RdF32(c.esp + 4), true);
  }
  CallGuest(c, 0x00A35CA0, {});
  c.esp += 8;  // its ret 8
}

// The rooms' blend's strength as it fades in and out: movss [00F498E0],
// xmm0. A room counted at full share takes it.
void hook_00A39EF6(Ctx& c) {
  const float fade = hook::Lane0(c.xmm[0]);
  hook::WrF32(kInteriorFade, fade);
  if (FfShadersOn()) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_room_last && !g_applied.empty() && g_applied.back().second == 1.0f) g_applied.back().second = fade;
    g_room_last = false;
  }
}

// timecyc.dat's lines (00A3B450): movss xmm0, [esp+2C4h], the far clip
// column, stored in the keyframe (+44h). 4500 m with volumetric fog.
void hook_00A3BB7B(Ctx& c) {
  hook::LoadSs(c.xmm[0], VolumetricFog() ? 4500.0f : hook::RdF32(c.esp + 0x2C4));
}

// The fog by the camera's height (00A3A2E0): movss xmm2, [013E4204], the
// far clip multiplier at the top (misc.Multiplier.farClipMultiplier); 1
// with volumetric fog.
void hook_00A3A3FB(Ctx& c) {
  hook::LoadSs(c.xmm[2], VolumetricFog() ? 1.0f : hook::RdF32(0x013E4204));
}

// movss xmm2, [013E4208]: the near fog multiplier at the top
// (misc.Multiplier.nearFogMultiplier); 1 with volumetric fog.
void hook_00A3A417(Ctx& c) {
  hook::LoadSs(c.xmm[2], VolumetricFog() ? 1.0f : hook::RdF32(0x013E4208));
}

// timecyclemodifiers*.dat's lines (00A353C0): movss xmm0, [esp+200h] and
// movss xmm0, [esp+158h], a modifier's min and max far clip, stored at
// +4 and +8 of its entry; 00A36790 applies each only when above 0. -1
// (none, as the game's own data writes it) with volumetric fog.
void hook_00A356AC(Ctx& c) {
  hook::LoadSs(c.xmm[0], VolumetricFog() ? -1.0f : hook::RdF32(c.esp + 0x200));
}
void hook_00A356BD(Ctx& c) {
  hook::LoadSs(c.xmm[0], VolumetricFog() ? -1.0f : hook::RdF32(c.esp + 0x158));
}

// A cutscene file's clip keyframe line ("%d %*d %f %f": frame, far clip,
// near clip; 007F4490): movss xmm0, [esp+3Ch], the far clip, stored in
// the keyframe (+8) that 007F0ED0 hands the camera (00A23490). 0 with
// volumetric fog: what the camera gets with no keyframe, so the
// timecycle's far clip stays.
void hook_007F5F02(Ctx& c) {
  hook::LoadSs(c.xmm[0], VolumetricFog() ? 0.0f : hook::RdF32(c.esp + 0x3C));
}

}  // namespace recomp::gen
