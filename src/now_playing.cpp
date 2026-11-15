// Now playing (GTAIV_NOW_PLAYING, off by default): in a vehicle, the
// song on the radio and its artist, as ZiT names them, shown at the top
// of the screen, under the radio's station name, for a few seconds when
// the player gets in, retunes, or the station moves on to another song.
//
// The game already knows: ZiT's script asks GET_AUDIBLE_MUSIC_TRACK_TEXT_ID
// (0080F310: the loudest station's track, through 009D64B0 its text id at
// the current play time, 1 for none: a small number, 136 say) and makes
// it the label TM_82_<id> (spcellphone.sco), whose text in the language's
// .gxt is "ZiT! Spotted ARTIST: Song"; GET_STRING_FROM_HASH_KEY (00B53A70)
// gives a label's text from its key, the label's hash. Here the same commands
// are asked a few times a second from the main loop; the text, without
// ZiT's "ZiT! Spotted ", is drawn with DISPLAY_TEXT_WITH_LITERAL_STRING
// each frame while it shows, as a script draws its own: right after the
// game empties the scripts' texts for the frame (008B07C0, called from the
// game's process, 00494DE0, before the scripts run), so that the frame's
// HUD draws it. (Added from the main loop's wait for the render thread,
// it was emptied before any frame drew it.)
//
//   GTAIV_NOW_PLAYING_LOG=1  logs each track id read and its text.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"
#include "script_natives.h"

namespace recomp::gen {

using namespace hook;

namespace {

using Clock = std::chrono::steady_clock;
using natives::kOut;

enum Command : uint32_t {
  GET_PLAYER_ID = 0x62E319C6,
  CONVERT_INT_TO_PLAYERINDEX = 0x5996315E,
  GET_PLAYER_CHAR = 0x511454A9,
  IS_PLAYER_PLAYING = 0x08274BA4,
  IS_PAUSE_MENU_ACTIVE = 0x6C4568A7,
  IS_SCREEN_FADED_OUT = 0x59EE3A11,
  IS_CHAR_IN_ANY_CAR = 0x71184DA3,
  IS_RADIO_RETUNING = 0x45C344AA,
  GET_AUDIBLE_MUSIC_TRACK_TEXT_ID = 0x18246AC8,
  GET_STRING_FROM_HASH_KEY = 0x16E14EA4,
  SET_TEXT_SCALE = 0x02C069E5,
  SET_TEXT_COLOUR = 0x19C967B5,
  SET_TEXT_DROPSHADOW = 0x58F5023F,
  SET_TEXT_EDGE = 0x2D7A725D,
  SET_TEXT_CENTRE = 0x204A6AA4,
  SET_TEXT_PROPORTIONAL = 0x15585A65,
  DISPLAY_TEXT_WITH_LITERAL_STRING = 0x661B239A,
};

// Guest stack taken below the main loop's: the commands' context
// (natives::kSize), then the label and the text they are handed.
constexpr uint32_t kLabel = natives::kSize, kText = kLabel + 0x10, kTextMax = 0xF0;
constexpr uint32_t kFrame = kText + kTextMax + 0x10;

// Where and how long: centred, under the station name the radio shows as
// it retunes (at 0.115 the two overlapped).
constexpr float kX = 0.5f, kY = 0.165f, kScaleX = 0.30f, kScaleY = 0.42f;
constexpr auto kShowFor = std::chrono::milliseconds(6000);
constexpr auto kFade = std::chrono::milliseconds(600);
constexpr auto kReadEvery = std::chrono::milliseconds(250);

// A command's handler, looked up once.
gaddr Handler(Ctx& c, Command command) {
  struct Known {
    uint32_t hash;
    gaddr handler;
  };
  static Known known[24];
  static int count = 0;
  for (int i = 0; i < count; i++)
    if (known[i].hash == command) return known[i].handler;
  const gaddr handler = natives::Find(c, command);
  if (handler && count < 24) known[count++] = {command, handler};
  return handler;
}

uint32_t Native(Ctx& c, uint32_t base, Command command, std::initializer_list<uint32_t> args) {
  const gaddr handler = Handler(c, command);
  return handler ? natives::Call(c, base, handler, args) : 0;
}

bool Flag(Ctx& c, uint32_t base, Command command, std::initializer_list<uint32_t> args) {
  return (Native(c, base, command, args) & 0xFF) != 0;
}

uint32_t Out(Ctx& c, uint32_t base, Command command, uint32_t arg) {
  Wr32(base + kOut, 0);
  Native(c, base, command, {arg, base + kOut});
  return Rd32(base + kOut);
}

uint32_t Float(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

bool On() {
  static const bool on = OptionOn("GTAIV_NOW_PLAYING", false);
  return on;
}

bool LogOn() {
  static const bool on = OptionOn("GTAIV_NOW_PLAYING_LOG", false);
  return on;
}

// A text label's key: the one-at-a-time hash of the label in lower case.
uint32_t LabelKey(const std::string& label) {
  uint32_t h = 0;
  for (char ch : label) {
    h += uint8_t(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  h += h << 15;
  return h;
}

// A guest string, at most `max` bytes.
std::string GuestString(uint32_t at, uint32_t max) {
  std::string s;
  if (!at) return s;
  for (uint32_t i = 0; i < max; i++) {
    const char ch = char(Rd8(at + i));
    if (!ch) break;
    s += ch;
  }
  return s;
}

void PutString(uint32_t at, const std::string& s, uint32_t max) {
  const uint32_t n = uint32_t(s.size() < max - 1 ? s.size() : max - 1);
  for (uint32_t i = 0; i < n; i++) Wr8(at + i, uint8_t(s[i]));
  Wr8(at + n, 0);
}

// "ZiT! Spotted THE WHO: The Seeker" (or a translation's "ZiT! Localizou
// ...") -> "THE WHO: The Seeker": ZiT's two first words left out. Empty
// for a text that is not ZiT's (a key the language does not have).
std::string Title(const std::string& zit) {
  if (zit.rfind("ZiT", 0) != 0) return {};
  size_t at = zit.find(' ');
  if (at != std::string::npos) at = zit.find(' ', at + 1);
  if (at == std::string::npos || at + 1 >= zit.size()) return {};
  return zit.substr(at + 1);
}

struct Shown {
  std::string text;
  Clock::time_point since{};
};

Shown g_shown;             // what shows, and since when
bool g_listening = false;  // read from the main loop

// What is read a few times a second.
struct Read {
  bool listening = false;  // playing, in a vehicle, nothing over the game
  bool retuning = false;
  uint32_t track = 0;
};

Read ReadGame(Ctx& c, uint32_t base) {
  Read r;
  const uint32_t player = Native(c, base, CONVERT_INT_TO_PLAYERINDEX, {Native(c, base, GET_PLAYER_ID, {})});
  const uint32_t ped = Out(c, base, GET_PLAYER_CHAR, player);
  r.listening = ped && Flag(c, base, IS_PLAYER_PLAYING, {player}) && !Flag(c, base, IS_PAUSE_MENU_ACTIVE, {}) &&
                !Flag(c, base, IS_SCREEN_FADED_OUT, {}) && !uint8_t(CallGuest(c, natives::kCutsceneRunning, {})) &&
                Flag(c, base, IS_CHAR_IN_ANY_CAR, {ped});
  if (r.listening) {
    r.retuning = Flag(c, base, IS_RADIO_RETUNING, {});
    r.track = Native(c, base, GET_AUDIBLE_MUSIC_TRACK_TEXT_ID, {});
  }
  return r;
}

void Draw(Ctx& c, uint32_t base, const std::string& text, uint8_t alpha) {
  PutString(base + kLabel, "STRING", 0x10);
  PutString(base + kText, text, kTextMax);
  Native(c, base, SET_TEXT_PROPORTIONAL, {1});
  Native(c, base, SET_TEXT_SCALE, {Float(kScaleX), Float(kScaleY)});
  Native(c, base, SET_TEXT_COLOUR, {255, 255, 255, alpha});
  Native(c, base, SET_TEXT_DROPSHADOW, {1, 0, 0, 0, alpha});
  Native(c, base, SET_TEXT_EDGE, {1, 0, 0, 0, alpha});
  Native(c, base, SET_TEXT_CENTRE, {1});
  Native(c, base, DISPLAY_TEXT_WITH_LITERAL_STRING, {Float(kX), Float(kY), base + kLabel, base + kText});
  // Left as a script expects it: not centred, no shadow or edge.
  Native(c, base, SET_TEXT_CENTRE, {0});
  Native(c, base, SET_TEXT_DROPSHADOW, {0, 0, 0, 0, 0});
  Native(c, base, SET_TEXT_EDGE, {0, 0, 0, 0, 0});
}

}  // namespace

// Once a frame, from the main loop.
void GtaivNowPlayingFrame(Ctx& c) {
  if (!On()) return;
  Shown& shown = g_shown;
  bool& listening = g_listening;
  static Clock::time_point next_read{};
  static uint32_t last_track = 0;

  KeepRegs keep(c);
  const uint32_t esp = c.esp;
  if (!CallGuest(c, natives::kFindPlayerPed, {})) {
    c.esp = esp;
    last_track = 0;
    listening = false;
    shown.text.clear();
    return;
  }
  c.esp = (esp - kFrame) & ~0xFu;
  const uint32_t base = c.esp;
  const Clock::time_point now = Clock::now();

  if (now >= next_read) {
    next_read = now + kReadEvery;
    const Read r = ReadGame(c, base);
    listening = r.listening;
    if (!r.listening) {
      last_track = 0;  // getting in again shows the song again
      shown.text.clear();
    } else if (!r.retuning && r.track != last_track) {
      last_track = r.track;
      if (r.track > 1) {
        const uint32_t key = LabelKey("TM_82_" + std::to_string(r.track));
        const uint32_t at = Native(c, base, GET_STRING_FROM_HASH_KEY, {key});
        const std::string zit = GuestString(at, 0x200);
        const std::string title = Title(zit);
        if (LogOn()) WarnF("gtaiv: now playing: track %08X \"%s\"\n", r.track, zit.c_str());
        if (!title.empty()) shown = {title, now};
      }
    }
  }

  c.esp = esp;
}

void f_008B07C0_lifted(Ctx& c);

// cdecl, no arguments: the scripts' texts emptied, once a frame (the
// frame counter [010FCC30] against [012085B0]); then the song, if one
// shows, is the frame's first text.
void f_008B07C0(Ctx& c) {
  f_008B07C0_lifted(c);
  if (!On() || !g_listening || g_shown.text.empty()) return;
  static uint32_t drawn_frame = 0;
  const uint32_t frame = Rd32(0x010FCC30);
  if (frame == drawn_frame) return;
  drawn_frame = frame;
  const Clock::time_point now = Clock::now();
  const auto age = now - g_shown.since;
  if (age >= kShowFor) {
    g_shown.text.clear();
    return;
  }
  const auto left = kShowFor - age;
  const float fade = float(std::chrono::duration_cast<Clock::duration>(kFade).count());
  float a = 1.0f;
  if (age < kFade) a = float(age.count()) / fade;
  if (left < kFade) a = float(left.count()) / fade;
  KeepRegs keep(c);
  const uint32_t esp = c.esp;
  c.esp = (esp - kFrame) & ~0xFu;
  Draw(c, c.esp, g_shown.text, uint8_t(a * 255.0f));
  c.esp = esp;
}

}  // namespace recomp::gen
