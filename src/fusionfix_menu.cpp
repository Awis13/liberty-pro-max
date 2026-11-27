// Native bindings for FusionFix 5.1.2's frontend rows on GTA IV 1.0.8.0.
// Based on the preference table and input hooks in FusionFix's settings.ixx (GPL-3.0).
#include <algorithm>
#include <atomic>
#include <cmath>
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
#include "fusionfix_options.inc"
#undef FUSION_OPTION
};

std::filesystem::path g_settings;
std::atomic<float> g_fov{0};

Preference* FindOption(const char* env) {
  for (auto& pref : g_preferences)
    if (std::strcmp(pref.env, env) == 0) return &pref;
  return nullptr;
}
}  // namespace

namespace gtaiv {

void LoadFusionFixSettings(const std::string& data_root) {
  g_settings = std::filesystem::path(data_root) / "fusionfix-settings";
  for (auto& pref : g_preferences) {
    int value = pref.initial;
    const bool fov = std::strcmp(pref.env, "GTAIV_FOV") == 0;
    if (fov) {
      // The launcher and menu use 5-degree steps. Terminal runs also accept degrees.
      const char* text = std::getenv(pref.env);
      const float requested = text ? float(std::atof(text)) : 0.0f;
      if (std::isfinite(requested)) {
        g_fov.store(requested, std::memory_order_relaxed);
        value = int(std::clamp(requested / 5.0f, 0.0f, float(pref.maximum)));
      }
    } else {
      value = OptionOn(pref.env, pref.initial);
    }
    int saved;
    if (std::ifstream(g_settings / pref.name) >> saved && saved >= 0 && saved <= pref.maximum) {
      value = saved;
      if (fov) g_fov.store(float(saved * 5), std::memory_order_relaxed);
    }
    pref.value.store(value, std::memory_order_relaxed);
  }
}

bool FusionFixOption(const char* env) {
  auto* pref = FindOption(env);
  return pref && pref->value.load(std::memory_order_relaxed) != 0;
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
