// A repeatable benchmark scene: the game's own script commands (natives)
// put the player at a fixed place, facing a fixed way with the camera
// behind, at a fixed time of day and weather, once the game has been
// running for a while after loading. Input scripts then walk or drive the
// same route every run, whatever way the camera faced after loading.
//
//   GTAIV_BENCH=x,y,z,heading[,hour,weather]: the scene;
//   GTAIV_BENCH=log: only the player's position and heading every 2 s
//   (logged after placing too).
//
// And, unless GTAIV_PLAY_LOG=0, every 5 s of play a line in the game's log
// with where the player is, on foot or in a car, the time of day and the
// interior: a crash report then says where it happened.
//
// Natives are looked up and called as ScriptHook does (iv-sdk,
// CTheScripts::FindNativeAddress and NativeInvoke), from the main loop,
// right after it has waited for the render thread (008A1860).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"
#include "script_natives.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS && !TARGET_OS_TV
void GtaivTouchContext(bool in_game, bool in_vehicle, bool menu);  // touch_ios.mm
#endif

namespace recomp::gen {

void MainLoopWaitRender(Ctx& c);  // perf.cpp: 008A1860's lifted body, timed

namespace {

using natives::kFindPlayerPed;
using natives::kOut;
using natives::kSize;

enum : uint32_t {
  GET_PLAYER_ID = 0x62E319C6,
  CONVERT_INT_TO_PLAYERINDEX = 0x5996315E,
  GET_PLAYER_CHAR = 0x511454A9,
  GET_CHAR_COORDINATES = 0x2B5C06E6,
  GET_CHAR_HEADING = 0x057A3AC7,
  SET_CHAR_COORDINATES = 0x689D0F5F,
  SET_CHAR_HEADING = 0x46B5523B,
  SET_CAM_BEHIND_PED = 0x48740598,
  LOAD_SCENE = 0x39F62BFB,
  FORCE_TIME_OF_DAY = 0x0B9B5070,
  FORCE_WEATHER_NOW = 0x63737D31,
  IS_CHAR_IN_ANY_CAR = 0x71184DA3,
  IS_PAUSE_MENU_ACTIVE = 0x6C4568A7,
  GET_TIME_OF_DAY = 0x384B3876,
  GET_INTERIOR_FROM_CHAR = 0x028227F7,
};

uint32_t Bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

float Float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// Calls native `hash` with 4-byte arguments; returns its first result.
uint32_t Native(Ctx& c, uint32_t base, uint32_t hash, std::initializer_list<uint32_t> args) {
  const gaddr fn = natives::Find(c, hash);
  if (!fn) {
    WarnF("gtaiv: bench: no native %08X\n", hash);
    return 0;
  }
  return natives::Call(c, base, fn, args);
}

struct Scene {
  bool log = false;
  float x = 0, y = 0, z = 0, heading = 0;
  int hour = -1, weather = -1;
};

const Scene& Config() {
  static const Scene s = [] {
    Scene s;
    const char* e = std::getenv("GTAIV_BENCH");
    if (!e || !*e) return s;
    if (!std::strcmp(e, "log")) {
      s.log = true;
      return s;
    }
    std::sscanf(e, "%f,%f,%f,%f,%d,%d", &s.x, &s.y, &s.z, &s.heading, &s.hour, &s.weather);
    return s;
  }();
  return s;
}

// The play log (GTAIV_PLAY_LOG, on unless 0).
bool PlayLog() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_PLAY_LOG");
    return !(e && *e == '0');
  }();
  return on;
}

// Whether 5 s have passed since the last play log line.
bool PlayLogDue() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last{};
  const Clock::time_point now = Clock::now();
  if (now - last < std::chrono::seconds(5)) return false;
  last = now;
  return true;
}

}  // namespace

void GtaivPadIconsFrame();  // pad_icons.cpp
void GtaivPadFeedbackFrame(Ctx& c);  // pad_feedback.cpp

// The main loop's wait for the render thread (thiscall, no arguments).
void f_008A1860(Ctx& c) {
  MainLoopWaitRender(c);
  GtaivPadIconsFrame();
  GtaivPadFeedbackFrame(c);
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS && !TARGET_OS_TV
  // The touch controls' layout, a few times a second.
  static uint32_t touch_frames = 0;
  if (++touch_frames % 10 == 0) {
    const uint32_t eax = c.eax, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
    const uint32_t esp = c.esp;
    const bool in_game = CallGuest(c, kFindPlayerPed, {}) != 0;
    bool in_vehicle = false, menu = false;
    if (in_game) {
      c.esp -= kSize;
      const uint32_t base = uint32_t(c.esp) & ~0xFu;
      c.esp = base;
      const uint32_t player =
          Native(c, base, CONVERT_INT_TO_PLAYERINDEX, {Native(c, base, GET_PLAYER_ID, {})});
      Native(c, base, GET_PLAYER_CHAR, {player, base + kOut});
      in_vehicle = (Native(c, base, IS_CHAR_IN_ANY_CAR, {Rd32(base + kOut)}) & 0xFF) != 0;
      menu = (Native(c, base, IS_PAUSE_MENU_ACTIVE, {}) & 0xFF) != 0;
    }
    c.eax = eax;
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    c.ebp = ebp;
    c.esp = esp;
    ::GtaivTouchContext(in_game, in_vehicle, menu);
  }
#endif
  const Scene& s = Config();
  if (!s.log && s.x == 0 && s.y == 0 && !PlayLog()) return;

  // Frames since the player first existed: the scene is set up once the
  // world has settled after loading.
  static uint32_t frames = 0;
  static bool placed = false;
  const uint32_t eax = c.eax, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t esp = c.esp;
  const bool bench = s.log || s.x != 0 || s.y != 0;
  if (!CallGuest(c, kFindPlayerPed, {})) {
    frames = 0;
  } else if (++frames >= 120 && (bench || PlayLogDue())) {
    c.esp -= kSize;
    const uint32_t base = uint32_t(c.esp) & ~0xFu;
    c.esp = base;
    const uint32_t player =
        Native(c, base, CONVERT_INT_TO_PLAYERINDEX, {Native(c, base, GET_PLAYER_ID, {})});
    Native(c, base, GET_PLAYER_CHAR, {player, base + kOut});
    const uint32_t ped = Rd32(base + kOut);
    if (PlayLog() && (!bench || PlayLogDue())) {
      Native(c, base, GET_CHAR_COORDINATES, {ped, base + kOut, base + kOut + 4, base + kOut + 8});
      Native(c, base, GET_CHAR_HEADING, {ped, base + kOut + 12});
      const bool car = (Native(c, base, IS_CHAR_IN_ANY_CAR, {ped}) & 0xFF) != 0;
      const float x = Float(Rd32(base + kOut)), y = Float(Rd32(base + kOut + 4)), z = Float(Rd32(base + kOut + 8)),
                  h = Float(Rd32(base + kOut + 12));
      Native(c, base, GET_TIME_OF_DAY, {base + kOut, base + kOut + 4});
      const uint32_t hour = Rd32(base + kOut), minute = Rd32(base + kOut + 4);
      Native(c, base, GET_INTERIOR_FROM_CHAR, {ped, base + kOut});
      WarnF("gtaiv: play: at %.1f,%.1f,%.1f heading %.0f, %s, %02u:%02u, interior %u\n", x, y, z, h,
            car ? "in a car" : "on foot", hour, minute, Rd32(base + kOut));
    }
    if (!bench) {
    } else if (!s.log && !placed) {
      placed = true;
      if (s.hour >= 0) Native(c, base, FORCE_TIME_OF_DAY, {uint32_t(s.hour), 0});
      if (s.weather >= 0) Native(c, base, FORCE_WEATHER_NOW, {uint32_t(s.weather)});
      Native(c, base, LOAD_SCENE, {Bits(s.x), Bits(s.y), Bits(s.z)});
      Native(c, base, SET_CHAR_COORDINATES, {ped, Bits(s.x), Bits(s.y), Bits(s.z)});
      Native(c, base, SET_CHAR_HEADING, {ped, Bits(s.heading)});
      Native(c, base, SET_CAM_BEHIND_PED, {ped});
      WarnF("gtaiv: bench: player placed at %.2f,%.2f,%.2f heading %.1f\n", s.x, s.y, s.z,
            s.heading);
    } else if (frames % 240 == 0) {
      Native(c, base, GET_CHAR_COORDINATES, {ped, base + kOut, base + kOut + 4, base + kOut + 8});
      Native(c, base, GET_CHAR_HEADING, {ped, base + kOut + 12});
      WarnF("gtaiv: bench: player at %.2f,%.2f,%.2f heading %.1f\n", Float(Rd32(base + kOut)),
            Float(Rd32(base + kOut + 4)), Float(Rd32(base + kOut + 8)),
            Float(Rd32(base + kOut + 12)));
    }
  }
  c.eax = eax;
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = esp;
}

}  // namespace recomp::gen
