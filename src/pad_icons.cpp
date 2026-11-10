// The button icons of the pad in use: Xbox 360 (the game's own), Xbox One and
// Series, PS3, PS4, PS5 and Switch, when the update folder's button texture
// dictionary (update/pc/textures/buttons_360.wtd) has those sets beside the
// game's own, with a prefix on their names. GTAIV_PAD_ICONS turns it on, off
// by default.

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/hle/kernel32_state.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

void f_00887E80_lifted(Ctx& c);

namespace {

constexpr gaddr kLoadTexture = 0x0085DF40;     // thiscall: where to put it; its name
constexpr gaddr kReleaseTexture = 0x0085CE80;  // thiscall: the slot (empty ones too)

// The slots the game fills with its button textures, in its order.
struct Slot {
  const char* name;
  gaddr at;
};
constexpr Slot kSlots[] = {
    {"UP_ARROW", 0x011EFB64},
    {"DOWN_ARROW", 0x011EFB68},
    {"LEFT_ARROW", 0x011EFB6C},
    {"RIGHT_ARROW", 0x011EFB70},
    {"DPAD_UP", 0x011EFB74},
    {"DPAD_DOWN", 0x011EFB78},
    {"DPAD_LEFT", 0x011EFB7C},
    {"DPAD_RIGHT", 0x011EFB80},
    {"DPAD_NONE", 0x011EFB84},
    {"DPAD_ALL", 0x011EFB88},
    {"DPAD_LEFTRIGHT", 0x011EFB90},
    {"DPAD_UPDOWN", 0x011EFB8C},
    {"LSTICK_UP", 0x011EFB94},
    {"LSTICK_DOWN", 0x011EFB98},
    {"LSTICK_LEFT", 0x011EFB9C},
    {"LSTICK_RIGHT", 0x011EFBA0},
    {"LSTICK_NONE", 0x011EFBA4},
    {"LSTICK_ALL", 0x011EFBA8},
    {"LSTICK_LEFTRIGHT", 0x011EFBB0},
    {"LSTICK_UPDOWN", 0x011EFBAC},
    {"RSTICK_UP", 0x011EFBB4},
    {"RSTICK_DOWN", 0x011EFBB8},
    {"RSTICK_LEFT", 0x011EFBBC},
    {"RSTICK_RIGHT", 0x011EFBC0},
    {"RSTICK_NONE", 0x011EFBC4},
    {"RSTICK_ALL", 0x011EFBC8},
    {"RSTICK_LEFTRIGHT", 0x011EFBD0},
    {"RSTICK_UPDOWN", 0x011EFBCC},
    {"A_BUTT", 0x011EFBD4},
    {"B_BUTT", 0x011EFBD8},
    {"Y_BUTT", 0x011EFBE0},
    {"X_BUTT", 0x011EFBDC},
    {"LB_BUTT", 0x011EFBE4},
    {"LT_BUTT", 0x011EFBE8},
    {"RB_BUTT", 0x011EFBEC},
    {"RT_BUTT", 0x011EFBF0},
    {"START_BUTT", 0x011EFBF4},
    {"BACK_BUTT", 0x011EFBF8},
    {"A_BUTT", 0x011EFBFC},
    {"B_BUTT", 0x011EFC00},
};

const char* const kPrefixes[] = {"", "XBONE_", "PS3_", "PS4_", "PS5_", "SWITCH_"};
constexpr size_t kPrefixCount = sizeof kPrefixes / sizeof kPrefixes[0];
constexpr size_t kSlotCount = sizeof kSlots / sizeof kSlots[0];

gaddr g_slots = 0;
uint32_t SlotOf(size_t prefix, size_t slot) { return g_slots + uint32_t(prefix * kSlotCount + slot) * 4; }
bool Complete(size_t prefix) {
  for (size_t s = 0; s < kSlotCount; s++)
    if (!Rd32(SlotOf(prefix, s))) return false;
  return true;
}
bool g_loaded = false;
int g_shown = 0;

bool On() {
  static const bool on = OptionOn("GTAIV_PAD_ICONS", false);
  return on;
}

int PrefixFor(SDL_GamepadType type) {
  switch (type) {
    case SDL_GAMEPAD_TYPE_XBOXONE: return 1;
    case SDL_GAMEPAD_TYPE_PS3: return 2;
    case SDL_GAMEPAD_TYPE_PS4: return 3;
    case SDL_GAMEPAD_TYPE_PS5: return 4;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return 5;
    default: return 0;
  }
}

int Wanted() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  const int prefix = count ? PrefixFor(SDL_GetGamepadTypeForID(ids[0])) : 0;
  SDL_free(ids);
  return prefix;
}

}  // namespace

// After the game has loaded its own button textures (push ebp, replaced): the others.
void hook_0088798D(Ctx& c) {
  Push(c, c.ebp);
  if (!On()) return;
  KeepRegs keep(c);
  const uint32_t esp = c.esp;  // (KeepRegs leaves it)
  const uint32_t base = (esp - 0x200) & ~0xFu;
  c.esp = base;
  const uint32_t name = base + 0x110;
  // In lasting memory: the game keeps where a texture's pointer is, to clear it
  // later. Made once: a reload fills the same slots again.
  if (!g_slots) g_slots = HeapAllocProcess(kPrefixCount * kSlotCount * 4, true);
  int complete = 0;
  for (size_t p = 0; p < kPrefixCount; p++) {
    bool all = true;
    for (size_t s = 0; s < kSlotCount; s++) {
      const uint32_t slot = SlotOf(p, s);
      if (p == 0) {
        Wr32(slot, Rd32(kSlots[s].at));
        continue;
      }
      const std::string text = std::string(kPrefixes[p]) + kSlots[s].name;
      std::strncpy(reinterpret_cast<char*>(GPtr(name)), text.c_str(), 47);
      c.esp = base;  // (the callee takes its argument off, and CallGuest again)
      c.ecx = slot;
      CallGuest(c, kLoadTexture, {name});
      all = all && Rd32(slot);
    }
    complete += all;
  }
  c.esp = esp;
  g_loaded = true;
  g_shown = 0;
  WarnF("gtaiv: pad icons: %d sets\n", complete);
}

// The game releasing its button textures (CMenuManager's, before a reload or
// at the end): its slots get back the textures it loaded, which it releases
// itself, and the other sets loaded above are released here. Otherwise a set
// shown in its slots would be released in place of the game's own, which
// would never be, and the rest of the sets never at all.
void f_00887E80(Ctx& c) {
  if (g_loaded) {
    KeepRegs keep(c);
    for (size_t s = 0; s < kSlotCount; s++) Wr32(kSlots[s].at, Rd32(SlotOf(0, s)));
    for (size_t p = 1; p < kPrefixCount; p++) {
      for (size_t s = 0; s < kSlotCount; s++) {
        c.ecx = SlotOf(p, s);
        CallGuest(c, kReleaseTexture, {});
        Wr32(SlotOf(p, s), 0);
      }
    }
    g_loaded = false;
    g_shown = 0;
  }
  f_00887E80_lifted(c);
}

// Once a frame: the set of the pad in use.
void GtaivPadIconsFrame() {
  if (!On() || !g_loaded) return;
  static uint32_t frames = 0;
  if (++frames % 30 != 1) return;
  const int wanted = Wanted();
  if (wanted == g_shown || !Complete(size_t(wanted))) return;
  for (size_t s = 0; s < kSlotCount; s++) Wr32(kSlots[s].at, Rd32(SlotOf(size_t(wanted), s)));
  g_shown = wanted;
  WarnF("gtaiv: pad icons: %s\n", wanted ? kPrefixes[wanted] : "Xbox 360");
}

}  // namespace recomp::gen
