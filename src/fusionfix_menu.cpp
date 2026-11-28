// Native bindings for FusionFix 5.1.2's frontend rows on GTA IV 1.0.8.0.
// Based on the preference table and input hooks in FusionFix's settings.ixx (GPL-3.0).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <charconv>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <string>
#include <strings.h>
#include <unistd.h>

#include "hook_util.h"
#include "option.h"
#include "runtime/log.h"

namespace {
struct Preference {
  const char* name;
  const char* env;
  int initial;
  int maximum;
  std::atomic<int> value{0};
};

Preference g_preferences[] = {
#define FUSION_OPTION(name, env, initial, maximum) {#name, #env, initial, maximum},
#define FUSION_SHADER_OPTION FUSION_OPTION
#include "fusionfix_options.inc"
#undef FUSION_SHADER_OPTION
#undef FUSION_OPTION
};

std::filesystem::path g_settings;
std::atomic<float> g_fov{0};
std::atomic<bool> g_timecycle_dirty{false};
double g_custom_fps = 0;
constexpr int kFrameRates[] = {0, 0, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240};
constexpr uint32_t kNoBlockOnLostFocus = 0x00FE80D3;


Preference* FindOption(const char* env) {
  for (auto& pref : g_preferences)
    if (std::strcmp(pref.env, env) == 0) return &pref;
  return nullptr;
}
// Seed settings from the existing launchers and command-line options. Once
// a player changes a FusionFix setting, its saved value takes precedence.
int InitialChoice(const Preference& pref) {
  const std::string_view name(pref.env);
  if (const char* text = std::getenv(pref.env); text && *text) {
    if (pref.maximum == 1) return *text != '0';
    const auto end = text + std::strlen(text);
    int value;
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec == std::errc{} && parsed.ptr == end)
      return std::clamp(value, 0, pref.maximum);
    const char* const tree_modes[] = {"pc", "pc+", "console"};
    const char* const shadow_modes[] = {"sharp", "soft", "chss"};
    if (name == "GTAIV_FF_SHADOW_FILTER") {
      for (int index = 0; index < 3; ++index)
        if (strcasecmp(text, shadow_modes[index]) == 0) return index;
    }
    if (name == "GTAIV_FF_TREE_LIGHTING" || name == "GTAIV_FF_TREE_ALPHA") {
      for (int index = 0; index < 3; ++index)
        if (strcasecmp(text, tree_modes[index]) == 0) return index;
    }
  }
  if (name == "GTAIV_FF_KB_FOOT_DELAY") return OptionOn("GTAIV_MOUSE_CAMERA_DELAY", true) ? 4 : 0;
  if (name == "GTAIV_FF_PAD_FOOT_DELAY") return OptionOn("GTAIV_PAD_CAMERA_DELAY", false) ? 4 : 0;
  if (name == "GTAIV_FF_KB_VEHICLE_DELAY" || name == "GTAIV_FF_PAD_VEHICLE_DELAY")
    return OptionOn("GTAIV_CAR_CAMERA_DELAY", false) ? 4 : 0;
  if (name == "GTAIV_FF_KB_VEHICLE_SPEED") return OptionOn("GTAIV_CAR_CAMERA_RETURN", true) ? 3 : 0;
  if (name == "GTAIV_FF_BLOCK_ON_FOCUS_LOSS") return !OptionOn("GTAIV_RUN_IN_BACKGROUND", false);
  if (name == "GTAIV_FF_BUTTON_STYLE" && OptionOn("GTAIV_PAD_ICONS", false)) return 8;
  if (name == "GTAIV_FF_FPS_PRESET" && g_custom_fps > 0) {
    for (size_t i = 2; i < std::size(kFrameRates); ++i)
      if (kFrameRates[i] == g_custom_fps) return int(i);
    return 1;
  }
  if (name == "GTAIV_FF_CONSOLE_GAMMA") {
    const char* curve = std::getenv("RECOMP_OUTPUT_CURVE");
    if (curve && std::strcmp(curve, "bt709") == 0) return 1;
    if (curve && std::strcmp(curve, "1.2") == 0) return 2;
  }
  return pref.initial;
}
}  // namespace

namespace gtaiv {

void LoadFusionFixSettings(const std::string& data_root) {
  g_settings = std::filesystem::path(data_root) / "fusionfix-settings";
  if (const char* fps = std::getenv("GTAIV_FPS_LIMIT")) {
    const double requested = std::atof(fps);
    if (std::isfinite(requested) && requested > 0) g_custom_fps = requested;
    // Apply the live preset in the game frame hook. Avoid a second limiter
    // in x86recomp's PresentFrame.
    unsetenv("GTAIV_FPS_LIMIT");
  }
  for (auto& pref : g_preferences) {
    int value = InitialChoice(pref);
    const bool fov = std::strcmp(pref.env, "GTAIV_FOV") == 0;
    if (fov) {
      // The launcher and menu use 5-degree steps. Terminal runs also accept degrees.
      const char* text = std::getenv(pref.env);
      const float requested = text ? float(std::atof(text)) : 0.0f;
      if (std::isfinite(requested)) {
        g_fov.store(requested, std::memory_order_relaxed);
        value = int(std::clamp(requested / 5.0f, 0.0f, float(pref.maximum)));
      }
    }
    int saved;
    if (std::ifstream(g_settings / pref.name) >> saved && saved >= 0 && saved <= pref.maximum) {
      value = saved;
      if (fov) g_fov.store(float(saved * 5), std::memory_order_relaxed);
      if (std::strcmp(pref.env, "GTAIV_FF_CONSOLE_GAMMA") == 0) {
        if (saved == 0) unsetenv("RECOMP_OUTPUT_CURVE");
        else setenv("RECOMP_OUTPUT_CURVE", saved == 1 ? "bt709" : "1.2", 1);
      }
    }
    pref.value.store(value, std::memory_order_relaxed);
  }
}

int FusionFixChoice(const char* env) {
  auto* pref = FindOption(env);
  return pref ? pref->value.load(std::memory_order_relaxed) : 0;
}

bool FusionFixOption(const char* env) { return FusionFixChoice(env) != 0; }

double FusionFixFrameLimit() {
  const int preset = FusionFixChoice("GTAIV_FF_FPS_PRESET");
  return preset == 1 ? g_custom_fps : kFrameRates[preset];
}

float FusionFixFov() {
  return g_fov.load(std::memory_order_relaxed);
}

}  // namespace gtaiv

namespace recomp::gen {
namespace {
// The 1.0.8.0 preference name table holds 0xA5 native entries.
constexpr uint32_t kFirstFusionPreference = 0xA5;
constexpr uint32_t kPreferences = 0x010FC0B8;
constexpr uint32_t kScalers = 0x010FC3A8;

Preference* MenuPreference(uint32_t id) {
  const uint32_t index = id - kFirstFusionPreference;
  return index < std::size(g_preferences) ? &g_preferences[index] : nullptr;
}

uint32_t ReadPreference(uint32_t id) {
  if (auto* pref = MenuPreference(id)) return uint32_t(pref->value.load(std::memory_order_relaxed));
  return Rd32(kPreferences + id * 4);
}

void WritePreference(uint32_t id, uint32_t value) {
  auto* pref = MenuPreference(id);
  if (!pref) {
    Wr32(kPreferences + id * 4, value);
    return;
  }
  int next = std::clamp(int32_t(value), 0, pref->maximum);
  if (pref->value.exchange(next, std::memory_order_relaxed) == next) return;
  if (std::strcmp(pref->env, "GTAIV_FOV") == 0) g_fov.store(float(next * 5), std::memory_order_relaxed);
  if (std::strcmp(pref->env, "GTAIV_FF_BLOCK_ON_FOCUS_LOSS") == 0)
    Wr8(kNoBlockOnLostFocus, next == 0);
  if (std::strcmp(pref->name, "PREF_BLOOM") == 0 || std::strcmp(pref->name, "PREF_TIMECYC") == 0)
    g_timecycle_dirty.store(true, std::memory_order_release);
  std::error_code ec;
  std::filesystem::create_directories(g_settings, ec);
  const auto path = g_settings / pref->name;
  const auto temporary = path.string() + "." + std::to_string(getpid()) + ".tmp";
  if (std::ofstream(temporary) << next << '\n') {
    std::filesystem::rename(temporary, path, ec);
    if (!ec) return;
  }
  std::filesystem::remove(temporary, ec);
  WarnF("gtaiv: cannot save FusionFix preference %s\n", pref->name);
}

void WriteScaler(uint32_t id, uint32_t value) {
  if (!MenuPreference(id)) Wr32(kScalers + id * 4, value);
}
}  // namespace

// The main loop calls this after the render thread is done with the old
// frame. The game's initializer reads the current episode's timecycle.
void GtaivTimecycleSettingsFrame(Ctx& c) {
  if (!g_timecycle_dirty.load(std::memory_order_acquire) || !Rd32(0x00F488EC) || Rd8(0x018CB06C)) return;
  if (!g_timecycle_dirty.exchange(false, std::memory_order_acq_rel)) return;
  hook::KeepRegs keep(c);
  CallGuest(c, 0x00A3B450, {});
  if (OptionOn("GTAIV_LOAD_LOG", false)) WarnF("gtaiv: FusionFix timecycle reloaded\n");
}

void f_0049BD90_lifted(Ctx& c);
void f_0049BD90(Ctx& c) {
  if (c.edi) {
    const char* name = reinterpret_cast<const char*>(GPtr(c.edi));
    for (size_t i = 0; i < std::size(g_preferences); ++i) {
      if (strcasecmp(name, g_preferences[i].name) != 0) continue;
      c.eax = kFirstFusionPreference + uint32_t(i);
      c.esp += 4;
      return;
    }
  }
  f_0049BD90_lifted(c);
}

void hook_0048DB56(Ctx& c) { WriteScaler(c.ecx, c.edx); }
void hook_0048E4B7(Ctx& c) { WriteScaler(c.ebp, c.edi); }
void hook_0049BD38(Ctx& c) { WriteScaler(c.ecx, c.eax); }
void hook_007BEF34(Ctx& c) {
  if (auto* pref = MenuPreference(c.eax)) c.eax = uint32_t(pref->maximum + 1);
  else c.eax = Rd32(kScalers + c.eax * 4);
}
void hook_007C3F0F(Ctx& c) { c.ecx = ReadPreference(c.ecx); }
void hook_007C4785(Ctx& c) { hook::SetLane0(c.xmm[1], float(int32_t(ReadPreference(c.eax)))); }
void hook_007C9CE4(Ctx& c) { c.ebx = ReadPreference(c.ecx); }
void hook_007C9DDB(Ctx& c) { hook::CmpFlags<4>(c, ReadPreference(c.edx), 0); }
void hook_007C9E47(Ctx& c) { WritePreference(c.ecx, c.ebx); }
void hook_007CA193(Ctx& c) { hook::CmpFlags<4>(c, c.edx, ReadPreference(c.edi)); }
void hook_007CA1A0(Ctx& c) { WritePreference(c.edi, c.edx); }
}  // namespace recomp::gen
