// Names, descriptions and gamerscore for the PC / Games for Windows Live set.
//
// Sources (public; this tree does not ship the game):
//   * Script indices 1-50, and that 51+ continues into the episodes:
//     https://gtamods.com/wiki/AWARD_ACHIEVEMENT
//     https://gtaforums.com/topic/387783-generic-gta-4-documentation/page/2/
//     (NTAuthority, March 2010: 51 is TLAD One Percenter, 65 is TBoGT Gold Star)
//   * Base-game order, official strings and points, Xbox 360 title 0x5454083B
//     (the same GFWL achievements the PC version uses):
//     https://dbox.tools/achievements/v1/5454083B/
//   * Points and requirements, including both episodes (1000 + 250 + 250):
//     https://gta.wiki/wiki/Achievements_in_GTA_IV
//   * Episode order cross-check. PSN inserts a platinum and two DLC headers,
//     so its numbers are not the write ids; after those, TLAD then TBoGT
//     ends at Gold Star, which matches the script range 51-65:
//     https://psntrophyleaders.com/game/view/gta-iv-ps3
//
// The PC exe imports the write (ordinal 5278) and the enumerator (5280).
// Hooking the write is what records an unlock. The ids below are the script
// indices. If a build ever writes a different number, it is still stored.

#include "achievements.h"

#include "option.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <numbers>

#include <sys/stat.h>

namespace gtaiv::achievements {

bool IsStyleName(const char* text);

namespace {

struct Def {
  uint32_t id;
  Game game;
  int points;
  const char* name;
  const char* description;
};

// clang-format off
const Def kCatalog[] = {
    {1, Game::iv, 5, "Off The Boat", "You have completed the first mission."},
    {2, Game::iv, 10, "One Hundred And Eighty", "You scored 180 with 3 darts."},
    {3, Game::iv, 10, "Pool Shark", "You beat a friend at pool."},
    {4, Game::iv, 15, "King of QUB3D", "You beat the high score in QUB3D."},
    {5, Game::iv, 15, "Finish Him", "You completed 10 melee counters in 4 minutes."},
    {6, Game::iv, 25, "Genetically Superior", "You came first in 20 single player street races."},
    {7, Game::iv, 30, "Wheelie Rider", "You completed a 500 foot wheelie on a motorbike."},
    {8, Game::iv, 10, "Gobble Gobble", "You got a turkey in 10-pin bowling."},
    {9, Game::iv, 10, "Driving Mr. Bellic", "You unlocked the special ability of Roman's taxi."},
    {10, Game::iv, 30, "Rolled Over", "You managed 5 car rolls in a row from one crash."},
    {11, Game::iv, 50, "Walk Free", "You successfully lost a 4 star wanted rating."},
    {12, Game::iv, 10, "Courier Service", "You completed all 10 package delivery jobs."},
    {13, Game::iv, 10, "Retail Therapy", "You unlocked the special ability of buying guns from Little Jacob."},
    {14, Game::iv, 20, "Chain Reaction", "You exploded 10 vehicles in 10 seconds."},
    {15, Game::iv, 40, "One Man Army", "You survived a 6 star wanted level for a full 5 minutes."},
    {16, Game::iv, 5, "Lowest Point", "You completed the mission Roman's Sorrow."},
    {17, Game::iv, 10, "Order Fulfilled", "You completed all 10 Exotic Export orders."},
    {18, Game::iv, 15, "Manhunt", "You completed the most wanted side mission."},
    {19, Game::iv, 20, "Cleaned The Mean Streets", "You caught 20 criminals through the police computer."},
    {20, Game::iv, 5, "Fed The Fish", "You completed the mission Uncle Vlad."},
    {21, Game::iv, 5, "It'll Cost Ya", "You made a taxi trip from one island to another without skipping."},
    {22, Game::iv, 5, "Sightseer", "You have taken all variations of the helicopter tours of Liberty City."},
    {23, Game::iv, 5, "Warm Coffee", "You were invited into your girlfriend's house."},
    {24, Game::iv, 10, "That's How We Roll!", "You unlocked the special ability of Brucie's helicopter ride."},
    {25, Game::iv, 55, "Half Million", "You have amassed a fortune of $500,000."},
    {26, Game::iv, 10, "Impossible Trinity", "You completed the mission Museum Piece."},
    {27, Game::iv, 20, "Full Exploration", "You unlocked all the islands."},
    {28, Game::iv, 20, "You Got The Message", "You delivered all 30 cars ordered through text message."},
    {29, Game::iv, 30, "Dare Devil", "You completed all the unique stunt jumps."},
    {30, Game::iv, 20, "Assassin's Greed", "You completed all 9 assassin missions."},
    {31, Game::iv, 50, "Endangered Species", "You collected every hidden package in the game."},
    {32, Game::iv, 40, "Under The Radar", "You flew under all the main bridges in the game."},
    {33, Game::iv, 10, "Dial B For Bomb", "You unlocked the special ability of Packie's car bomb placement."},
    {34, Game::iv, 10, "Gracefully Taken", "You completed the mission I'll Take Her."},
    {35, Game::iv, 20, "Liberty City (5)", "Your friends all like you above 90%."},
    {36, Game::iv, 5, "No More Strangers", "You've met all the random characters."},
    {37, Game::iv, 10, "That Special Someone", "You completed the mission That Special Someone."},
    {38, Game::iv, 60, "You Won!", "You have completed the story."},
    {39, Game::iv, 30, "Liberty City Minute", "You finished the story in less than 30 hours."},
    {40, Game::iv, 100, "Key To The City", "You completed GTA IV to 100%."},
    {41, Game::iv, 10, "Teamplayer", "You killed 5 opposing team members."},
    {42, Game::iv, 5, "Cut Your Teeth", "You were successfully promoted up a rank in multiplayer."},
    {43, Game::iv, 10, "Join The Midnight Club", "You won a ranked multiplayer race without damaging your vehicle too much."},
    {44, Game::iv, 15, "Fly The Co-op", "You completed Deal Breaker, Hangman's Noose and Bomb Da Base II in time."},
    {45, Game::iv, 10, "Taking It For The Team", "You were on the winning team in all multiplayer team games."},
    {46, Game::iv, 10, "Top Of The Food Chain", "You killed 20 players with a pistol in a deathmatch."},
    {47, Game::iv, 20, "Top The Midnight Club", "You won 20 different multiplayer standard races."},
    {48, Game::iv, 20, "Wanted", "You achieved the highest personal rank possible in multiplayer."},
    {49, Game::iv, 30, "Auf Wiedersehen Petrovic", "You won in all variations of multiplayer games."},
    {50, Game::iv, 10, "Let Sleeping Rockstars Lie", "You killed a Rockstar in multiplayer."},
    {51, Game::tlad, 5, "TLAD: One Percenter", "Help Billy get his bike back."},
    {52, Game::tlad, 25, "TLAD: The Lost Boy", "Become leader of The Lost."},
    {53, Game::tlad, 100, "TLAD: Easy Rider", "Finish the story."},
    {54, Game::tlad, 50, "TLAD: Get Good Wood", "In the bike races, whack 69 bikers off their bikes with a bat."},
    {55, Game::tlad, 70, "TLAD: Full Chat", "Build Terry and Clay's toughness to 100%."},
    {56, Game::tbogt, 5, "TBoGT: Gone Down", "Complete all base jumps."},
    {57, Game::tbogt, 5, "TBoGT: Diamonds Forever", "Complete the Trinity."},
    {58, Game::tbogt, 10, "TBoGT: Four Play", "Hit a flag with a golf ball four times."},
    {59, Game::tbogt, 15, "TBoGT: Bear Fight", "Win the L.C. Cage Fighters championship."},
    {60, Game::tbogt, 15, "TBoGT: Catch the Bus", "Dance perfectly in both of Tony's nightclubs."},
    {61, Game::tbogt, 20, "TBoGT: Snow Queen", "Complete 25 drug wars."},
    {62, Game::tbogt, 25, "TBoGT: Adrenaline Junkie", "Freefall for the longest possible time."},
    {63, Game::tbogt, 30, "TBoGT: Maestro", "Finish the Ballad."},
    {64, Game::tbogt, 45, "TBoGT: Past the Velvet Rope", "Score 80% or above in all missions."},
    {65, Game::tbogt, 80, "TBoGT: Gold Star", "Score 100% in all missions."},
};
// clang-format on

constexpr int kCount = int(std::size(kCatalog));

const Def* Lookup(uint32_t id) {
  if (id == 0 || int(id) > kCount) return nullptr;
  const Def& row = kCatalog[id - 1];
  return row.id == id ? &row : nullptr;
}

std::string ParentDir(const std::string& path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos) return {};
  return path.substr(0, slash);
}

void MakeDirs(const std::string& dir) {
  if (dir.empty()) return;
  std::string cur;
  for (char ch : dir) {
    cur.push_back(ch);
    if (ch == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
  }
  mkdir(dir.c_str(), 0755);
}

std::string ReadAll(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::string out;
  char buf[4096];
  while (size_t n = std::fread(buf, 1, sizeof buf, f)) out.append(buf, n);
  std::fclose(f);
  return out;
}

bool WriteAtomic(const std::string& path, const std::string& body) {
  MakeDirs(ParentDir(path));
  const std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const size_t n = std::fwrite(body.data(), 1, body.size(), f);
  const int closed = std::fclose(f);
  if (closed != 0 || n != body.size()) {
    std::remove(tmp.c_str());
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

std::string FormatUnix(int64_t unix_sec) {
  if (unix_sec < 0) unix_sec = 0;
  std::tm tm{};
  const std::time_t t = static_cast<std::time_t>(unix_sec);
  if (!gmtime_r(&t, &tm)) return "1970-01-01T00:00:00Z";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

bool ParseUnix(const std::string& text, int64_t* out) {
  int year, month, day, hour, minute, second;
  char zone = 0;
  if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d%c", &year, &month, &day, &hour, &minute, &second, &zone) < 6)
    return false;
  if (zone != 0 && zone != 'Z') return false;
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  const std::time_t t = timegm(&tm);
  if (t == static_cast<std::time_t>(-1) && year > 1971) return false;
  *out = static_cast<int64_t>(t);
  return true;
}

// Scanner for the object this file writes, plus unknown keys beside it.
struct Json {
  const std::string& s;
  size_t i = 0;
  bool bad = false;

  void Ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }
  bool Eat(char ch) {
    Ws();
    if (i >= s.size() || s[i] != ch) return false;
    i++;
    return true;
  }
  bool See(char ch) {
    Ws();
    return i < s.size() && s[i] == ch;
  }
  bool TakeString(std::string* out) {
    Ws();
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out->clear();
    while (i < s.size()) {
      const char ch = s[i++];
      if (ch == '"') return true;
      if (ch != '\\') {
        out->push_back(ch);
        continue;
      }
      if (i >= s.size()) return false;
      const char esc = s[i++];
      if (esc == 'u') {
        if (i + 4 > s.size()) return false;
        i += 4;
        out->push_back('?');
      } else if (esc == 'n') {
        out->push_back('\n');
      } else if (esc == 't') {
        out->push_back('\t');
      } else {
        out->push_back(esc);
      }
    }
    return false;
  }
  bool TakeInt(int64_t* out) {
    Ws();
    const size_t start = i;
    if (i < s.size() && s[i] == '-') i++;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    try {
      *out = std::stoll(s.substr(start, i - start));
    } catch (...) {
      return false;
    }
    return true;
  }
  bool SkipString() {
    std::string ignored;
    return TakeString(&ignored);
  }
  bool SkipValue();
};

bool Json::SkipValue() {
  Ws();
  if (i >= s.size()) return false;
  const char ch = s[i];
  if (ch == '"') return SkipString();
  if (ch == '-' || (ch >= '0' && ch <= '9')) {
    int64_t ignored = 0;
    return TakeInt(&ignored);
  }
  if (ch == 't' || ch == 'f' || ch == 'n') {
    while (i < s.size() && s[i] >= 'a' && s[i] <= 'z') i++;
    return true;
  }
  if (ch == '[') {
    i++;
    if (See(']')) return Eat(']');
    while (SkipValue()) {
      if (Eat(']')) return true;
      if (!Eat(',')) return false;
    }
    return false;
  }
  if (ch == '{') {
    i++;
    if (See('}')) return Eat('}');
    while (true) {
      std::string key;
      if (!TakeString(&key) || !Eat(':') || !SkipValue()) return false;
      if (Eat('}')) return true;
      if (!Eat(',')) return false;
    }
  }
  return false;
}

bool ParseUnlocks(const std::string& text, std::vector<Unlock>* out) {
  if (text.find_first_not_of(" \t\r\n") == std::string::npos) return true;
  Json js{text};
  if (!js.Eat('{')) return false;
  if (js.See('}')) return js.Eat('}');
  while (true) {
    std::string key;
    if (!js.TakeString(&key) || !js.Eat(':')) return false;
    if (key == "unlocks") {
      if (!js.Eat('[')) return false;
      if (!js.See(']')) {
        while (true) {
          if (!js.Eat('{')) return false;
          Unlock row;
          bool got_id = false, got_time = false;
          if (!js.See('}')) {
            while (true) {
              std::string field;
              if (!js.TakeString(&field) || !js.Eat(':')) return false;
              if (field == "id") {
                int64_t id = 0;
                if (!js.TakeInt(&id) || id <= 0 || id > 0xffffffffll) return false;
                row.id = uint32_t(id);
                got_id = true;
              } else if (field == "game") {
                std::string game;
                if (!js.TakeString(&game)) return false;
                row.game = GameFromKey(game);
              } else if (field == "unlocked_at") {
                std::string when;
                if (!js.TakeString(&when) || !ParseUnix(when, &row.unlocked_at)) return false;
                got_time = true;
              } else if (!js.SkipValue()) {
                return false;
              }
              if (js.Eat('}')) break;
              if (!js.Eat(',')) return false;
            }
          } else if (!js.Eat('}')) {
            return false;
          }
          if (got_id && got_time && row.unlocked_at >= 0) {
            if (row.game == Game::unknown) {
              if (const Def* known = Lookup(row.id)) row.game = known->game;
            }
            auto older = std::find_if(out->begin(), out->end(), [&](const Unlock& u) { return u.id == row.id; });
            if (older == out->end()) out->push_back(row);
            else if (row.unlocked_at < older->unlocked_at) *older = row;
          }
          if (js.Eat(']')) break;
          if (!js.Eat(',')) return false;
        }
      } else if (!js.Eat(']')) {
        return false;
      }
    } else if (!js.SkipValue()) {
      return false;
    }
    if (js.Eat('}')) return true;
    if (!js.Eat(',')) return false;
  }
}

std::string WriteJson(const std::vector<Unlock>& rows) {
  std::string out = "{\n  \"version\": 1,\n  \"unlocks\": [";
  for (size_t i = 0; i < rows.size(); i++) {
    const Unlock& row = rows[i];
    if (i) out += ',';
    out += "\n    {\"id\": ";
    out += std::to_string(row.id);
    out += ", \"game\": \"";
    out += GameKey(row.game);
    out += "\", \"unlocked_at\": \"";
    out += FormatUnix(row.unlocked_at);
    out += "\"}";
  }
  if (!rows.empty()) out += '\n';
  out += "  ]\n}\n";
  return out;
}

int64_t SteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::mutex g_mu;
ToastQueue g_queue;
struct PendingToast {
  Style style = Style::XboxA;
  int points = -1;
  std::string line1, line2, line3;
};
std::vector<PendingToast> g_waiting;  // earned before the city is on screen
bool g_test_armed = false;
bool g_saw_scene = false;
int64_t g_scene_ms = 0;
bool g_gate_open = false;

// stderr is the game's log: main freopens it onto ~/Library/Logs/gtaiv.log
// when the launcher set GTAIV_LOG, and the gfx lines ("gfx: first scene")
// are fprintf(stderr) too. Flush so a line is in the file before a hang.
void LogLine(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fflush(stderr);
}

void EnqueueLocked(const PendingToast& item, int64_t now) {
  if (g_gate_open) g_queue.Push(item.style, item.points, item.line1, item.line2, item.line3, now);
  else g_waiting.push_back(item);
}

PendingToast MakeToast(Style style, int points, const std::string& name, const std::string& description, bool vehicle) {
  const ToastCopy copy = CopyFor(style, points, name, description, vehicle);
  PendingToast item;
  item.style = style;
  item.points = vehicle ? -1 : points;
  item.line1 = copy.line1;
  item.line2 = copy.line2;
  item.line3 = copy.line3;
  return item;
}

void ArmTestLocked(int64_t now) {
  if (g_test_armed) return;
  g_test_armed = true;
  const char* env = std::getenv("GTAIV_ACHIEVEMENT_TEST");
  if (!env || !*env) return;
  Style style = CurrentStyle();
  uint32_t id = 1;
  if (IsStyleName(env)) {
    style = ParseStyle(env);
  } else if (env[0] >= '0' && env[0] <= '9') {
    id = uint32_t(std::strtoul(env, nullptr, 10));
    if (id == 0) id = 1;
  }
  const Entry* row = Find(id);
  const std::string name = row ? row->name : "Achievement " + std::to_string(id);
  const std::string description = row ? row->description : "";
  const int points = row ? row->points : -1;
  EnqueueLocked(MakeToast(style, points, name, description, false), now);
  LogLine("achievement: test toast %u style %s (GTAIV_ACHIEVEMENT_TEST, not saved)\n", id, StyleKey(style));
}

void OpenGateLocked(int64_t now) {
  if (g_gate_open) return;
  g_gate_open = true;
  ArmTestLocked(now);
  for (PendingToast& item : g_waiting)
    g_queue.Push(item.style, item.points, std::move(item.line1), std::move(item.line2), std::move(item.line3), now);
  g_waiting.clear();
  LogLine("achievements: toasts on\n");
}

float Smooth(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

}  // namespace

const char* GameKey(Game game) {
  switch (game) {
    case Game::iv: return "iv";
    case Game::tlad: return "tlad";
    case Game::tbogt: return "tbogt";
    case Game::unknown: return "unknown";
  }
  return "unknown";
}

Game GameFromKey(const std::string& key) {
  if (key == "iv") return Game::iv;
  if (key == "tlad") return Game::tlad;
  if (key == "tbogt") return Game::tbogt;
  return Game::unknown;
}

const Entry* Catalog() {
  static const auto rows = [] {
    std::vector<Entry> out;
    out.reserve(size_t(kCount));
    for (const Def& def : kCatalog)
      out.push_back(Entry{def.id, def.game, def.points, def.name, def.description});
    return out;
  }();
  return rows.data();
}

const Entry* Find(uint32_t id) {
  if (!Lookup(id)) return nullptr;
  return Catalog() + (id - 1);
}

int CatalogCount() { return kCount; }

std::string ToastDetail(int points, const std::string& name) {
  if (points < 0) return name;
  return std::to_string(points) + "G - " + name;
}

namespace {

std::string Lower(const char* text) {
  std::string s = text ? text : "";
  for (char& ch : s)
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
  return s;
}

const char* kStyleKeys[] = {"xbox_a", "xbox_b", "xbox_c", "xbox_d", "xbox_e", "ps3", "steam"};

float EaseOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

float EaseInOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
}

}  // namespace

Style ParseStyle(const char* text) {
  const std::string s = Lower(text);
  for (int i = 0; i < 7; i++)
    if (s == kStyleKeys[i]) return Style(i);
  return Style::XboxA;
}

const char* StyleKey(Style style) {
  const int i = int(style);
  if (i < 0 || i > 6) return kStyleKeys[0];
  return kStyleKeys[i];
}

Style CurrentStyle() { return ParseStyle(std::getenv("GTAIV_ACHIEVEMENT_STYLE")); }

Anchor StyleAnchor(Style style) {
  if (style == Style::Ps3) return Anchor::TopRight;
  if (style == Style::Steam) return Anchor::BottomRight;
  return Anchor::BottomCenter;
}

bool IsStyleName(const char* text) {
  const std::string s = Lower(text);
  for (const char* key : kStyleKeys)
    if (s == key) return true;
  return false;
}

const char* kPlaceKeys[] = {"default", "top_left", "top_right", "bottom_left", "bottom_right", "top", "bottom"};

Place ParsePlace(const char* text) {
  const std::string s = Lower(text);
  for (int i = 0; i < 7; i++)
    if (s == kPlaceKeys[i]) return Place(i);
  return Place::Default;
}

const char* PlaceKey(Place place) {
  const int i = int(place);
  if (i < 0 || i > 6) return kPlaceKeys[0];
  return kPlaceKeys[i];
}

Place CurrentPlace() { return ParsePlace(std::getenv("GTAIV_ACHIEVEMENT_POSITION")); }

std::string TitleCaseLabel(const std::string& text) {
  std::string out, word;
  auto flush = [&] {
    if (word.empty()) return;
    if (word.size() <= 2) {
      for (char& ch : word) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      word[0] = char(std::toupper(static_cast<unsigned char>(word[0])));
      for (size_t i = 1; i < word.size(); i++)
        word[i] = char(std::tolower(static_cast<unsigned char>(word[i])));
    }
    if (!out.empty()) out.push_back(' ');
    out += word;
    word.clear();
  };
  for (unsigned char ch : text) {
    if (std::isalnum(ch)) word.push_back(char(ch));
    else if (!word.empty()) flush();
  }
  flush();
  return out;
}

void CopyToastRows(const uint8_t* src, uint8_t* dst, int width, int height) {
  if (!src || !dst || width < 1 || height < 1) return;
  const size_t n = size_t(width) * size_t(height) * 4;
  if (src != dst) std::memcpy(dst, src, n);
}

Motion MotionAt(Style style, int64_t elapsed) {
  Motion m;
  if (elapsed < 0) return m;
  const bool xbox = int(style) <= int(Style::XboxE);
  if (xbox) {
    constexpr int64_t kOrb = 250, kExpand = 350, kHold = 5000, kCollapse = 350, kOut = 250;
    const int64_t t1 = kOrb, t2 = t1 + kExpand, t3 = t2 + kHold, t4 = t3 + kCollapse, t5 = t4 + kOut;
    if (elapsed >= t5) return m;
    m.visible = true;
    // Used when the toast is not in its own corner. Default Xbox placement
    // ignores it and grows the pill instead.
    if (elapsed < t2) m.slide = Smooth(float(elapsed) / float(t2));
    else if (elapsed < t3) m.slide = 1;
    else m.slide = 1.f - Smooth(float(elapsed - t3) / float(t5 - t3));
    if (elapsed < t1) {
      m.orb = Smooth(float(elapsed) / float(kOrb));
      m.fade = m.orb;
    } else if (elapsed < t2) {
      m.orb = 1;
      m.fade = 1;
      m.pill = Smooth(float(elapsed - t1) / float(kExpand));
    } else if (elapsed < t3) {
      m.orb = m.pill = m.fade = 1;
    } else if (elapsed < t4) {
      m.orb = 1;
      m.fade = 1;
      m.pill = 1.f - Smooth(float(elapsed - t3) / float(kCollapse));
    } else {
      m.pill = 0;
      m.orb = 1.f - Smooth(float(elapsed - t4) / float(kOut));
      m.fade = m.orb;
    }
    return m;
  }
  if (style == Style::Ps3) {
    constexpr int64_t kIn = 400, kHold = 5000, kOut = 1500;
    if (elapsed >= kIn + kHold + kOut) return m;
    m.visible = true;
    m.orb = m.pill = 1;
    if (elapsed < kIn) {
      m.slide = EaseOutCubic(float(elapsed) / float(kIn));
      m.fade = 1;
    } else if (elapsed < kIn + kHold) {
      m.slide = 1;
      m.fade = 1;
    } else {
      const float t = float(elapsed - kIn - kHold) / float(kOut);
      m.slide = 1.f - EaseInOutCubic(t);
      const float fade_t = (t * 1.5f - 0.5f);
      m.fade = 1.f - std::clamp(fade_t, 0.f, 1.f);
    }
    return m;
  }
  constexpr int64_t kIn = 350, kHold = 6000, kOut = 350;
  if (elapsed >= kIn + kHold + kOut) return m;
  m.visible = true;
  m.orb = m.pill = 1;
  m.fade = 1;
  if (elapsed < kIn) m.slide = Smooth(float(elapsed) / float(kIn));
  else if (elapsed < kIn + kHold) m.slide = 1;
  else m.slide = 1.f - Smooth(float(elapsed - kIn - kHold) / float(kOut));
  return m;
}

int64_t StyleDuration(Style style) {
  if (int(style) <= int(Style::XboxE)) return 250 + 350 + 5000 + 350 + 250;
  if (style == Style::Ps3) return 400 + 5000 + 1500;
  return 350 + 6000 + 350;
}

ToastBox PlaceToast(Style style, float slide, int view_w, int view_h) {
  ToastBox box;
  if (view_w < 8 || view_h < 8) return box;
  const float s = float(view_h) / 720.f;
  slide = std::clamp(slide, 0.f, 1.f);
  const Place place = CurrentPlace();
  if (place != Place::Default) {
    const bool ps3 = style == Style::Ps3;
    const bool steam = style == Style::Steam;
    box.w = std::max(1, int(std::lround((ps3 || steam ? 300.f : 432.f) * s)));
    box.h = std::max(1, int(std::lround((ps3 ? 72.f : steam ? 80.f : 82.f) * s)));
    if (box.w > view_w - 8) box.w = std::max(1, view_w - 8);
    const int margin = std::max(4, int(std::lround(0.06f * float(view_h))));
    const bool top = place == Place::Top || place == Place::TopLeft || place == Place::TopRight;
    const bool left = place == Place::TopLeft || place == Place::BottomLeft;
    const bool right = place == Place::TopRight || place == Place::BottomRight;
    const int rest_y = top ? margin : std::max(0, view_h - margin - box.h);
    int rest_x = std::max(0, (view_w - box.w) / 2);
    if (left) rest_x = margin;
    if (right) rest_x = std::max(0, view_w - margin - box.w);
    if (left || right) {
      const int off = left ? -box.w : view_w;
      box.x = int(std::lround(float(off) * (1.f - slide) + float(rest_x) * slide));
      box.y = rest_y;
    } else {
      const int off = top ? -box.h : view_h;
      box.x = rest_x;
      box.y = int(std::lround(float(off) * (1.f - slide) + float(rest_y) * slide));
    }
    return box;
  }
  if (style == Style::Ps3) {
    box.w = std::max(1, int(std::lround(300.f * s)));
    box.h = std::max(1, int(std::lround(72.f * s)));
    const int right = int(std::lround(68.f * s));
    const int top = int(std::lround(55.f * s));
    const int rest = view_w - right - box.w;
    box.x = int(std::lround(float(view_w) * (1.f - slide) + float(rest) * slide));
    box.y = top;
    return box;
  }
  if (style == Style::Steam) {
    box.w = std::max(1, int(std::lround(300.f * s)));
    box.h = std::max(1, int(std::lround(80.f * s)));
    const int margin = int(std::lround(16.f * s));
    box.x = std::max(0, view_w - margin - box.w);
    const int rest = view_h - margin - box.h;
    box.y = int(std::lround(float(view_h) * (1.f - slide) + float(rest) * slide));
    return box;
  }
  box.w = std::max(1, int(std::lround(432.f * s)));
  box.h = std::max(1, int(std::lround(82.f * s)));
  if (box.w > view_w - 8) box.w = std::max(1, view_w - 8);
  box.x = std::max(0, (view_w - box.w) / 2);
  const int margin = int(std::lround(0.08f * float(view_h)));
  box.y = std::max(0, view_h - margin - box.h);
  return box;
}

ToastCopy CopyFor(Style style, int points, const std::string& name, const std::string& description, bool vehicle) {
  ToastCopy copy;
  if (vehicle) {
    copy.line1 = "Entered";
    copy.line2 = name.empty() ? "Vehicle" : name;
    return copy;
  }
  if (style == Style::Ps3) {
    copy.line1 = "You have earned a trophy.";
    copy.line2 = name;
    return copy;
  }
  if (style == Style::Steam) {
    copy.line1 = "Achievement Unlocked!";
    copy.line2 = name;
    copy.line3 = description;
    return copy;
  }
  if (style == Style::XboxC) copy.line1 = "ACHIEVEMENT UNLOCKED";
  else if (style == Style::XboxE) copy.line1 = "Rare Achievement Unlocked";
  else copy.line1 = "Achievement Unlocked";
  copy.line2 = ToastDetail(points, name);
  return copy;
}

Store Store::Load(const std::string& path) {
  Store store;
  store.path = path;
  const std::string text = ReadAll(path);
  if (text.empty()) return store;
  std::vector<Unlock> rows;
  if (!ParseUnlocks(text, &rows)) {
    const std::string bak = path + ".bak";
    if (std::rename(path.c_str(), bak.c_str()) != 0) {
      store.save_blocked = true;
      std::fprintf(stderr, "achievement: %s is not a list this build can read, and it could not be moved aside\n",
                   path.c_str());
    } else {
      std::fprintf(stderr, "achievement: %s is not a list this build can read; kept as %s\n", path.c_str(),
                   bak.c_str());
    }
    return store;
  }
  store.unlocks_ = std::move(rows);
  return store;
}

bool Store::Save() const {
  if (save_blocked) return false;
  return WriteAtomic(path, WriteJson(unlocks_));
}

bool Store::Add(uint32_t id, int64_t unix_sec) {
  if (save_blocked || id == 0 || unix_sec < 0) return false;
  if (IsUnlocked(id)) return false;
  Unlock row;
  row.id = id;
  row.unlocked_at = unix_sec;
  if (const Def* known = Lookup(id)) row.game = known->game;
  unlocks_.push_back(row);
  std::sort(unlocks_.begin(), unlocks_.end(), [](const Unlock& a, const Unlock& b) { return a.id < b.id; });
  return true;
}

void Store::Clear() { unlocks_.clear(); }

bool Store::IsUnlocked(uint32_t id) const {
  return std::any_of(unlocks_.begin(), unlocks_.end(), [&](const Unlock& row) { return row.id == id; });
}

std::vector<Row> Rows(const Store& store) {
  std::vector<Row> rows;
  rows.reserve(size_t(kCount) + store.unlocks().size());
  for (int i = 0; i < kCount; i++) {
    const Def& def = kCatalog[i];
    Row row;
    row.id = def.id;
    row.game = def.game;
    row.name = def.name;
    row.description = def.description;
    row.points = def.points;
    if (const Unlock* got = [&]() -> const Unlock* {
          for (const Unlock& u : store.unlocks())
            if (u.id == def.id) return &u;
          return nullptr;
        }()) {
      row.unlocked = true;
      row.unlocked_at = got->unlocked_at;
    }
    rows.push_back(std::move(row));
  }
  for (const Unlock& extra : store.unlocks()) {
    if (Lookup(extra.id)) continue;
    Row row;
    row.id = extra.id;
    row.game = extra.game;
    row.name = "Achievement " + std::to_string(extra.id);
    row.description = "Not in the public GFWL table. Recorded because the game wrote this id.";
    row.points = -1;
    row.unlocked = true;
    row.unlocked_at = extra.unlocked_at;
    rows.push_back(std::move(row));
  }
  return rows;
}

Score Summarize(const Store& store) {
  Score score;
  score.catalog_count = kCount;
  for (const Def& def : kCatalog) score.catalog_points += def.points;
  for (const Row& row : Rows(store)) {
    if (!row.unlocked) continue;
    if (row.points < 0) score.unlocked_unknown++;
    else {
      score.unlocked_known++;
      score.unlocked_points += row.points;
    }
  }
  return score;
}

std::vector<EnumItem> UnlockedSlice(const Store& store, uint32_t start, uint32_t count) {
  std::vector<EnumItem> all;
  for (const Row& row : Rows(store)) {
    if (!row.unlocked) continue;
    EnumItem item;
    item.id = row.id;
    item.points = std::max(0, row.points);
    item.unlocked_at = row.unlocked_at;
    item.label = row.name;
    item.description = row.description;
    all.push_back(std::move(item));
  }
  std::vector<EnumItem> page;
  if (start >= all.size() || count == 0) return page;
  const size_t n = std::min(size_t(count), all.size() - size_t(start));
  page.insert(page.end(), all.begin() + ptrdiff_t(start), all.begin() + ptrdiff_t(start + n));
  return page;
}

uint32_t EnumLabelChars() { return 64; }
uint32_t EnumDescriptionChars() { return 180; }

uint32_t EnumBufferBytes(uint32_t count) {
  // XACHIEVEMENT_DETAILS is 36 bytes on 32-bit (three wide pointers). The
  // strings sit after the rows, in the same buffer the caller allocates
  // from the size reported here: label, description, unachieved.
  const uint32_t chars = EnumLabelChars() + 1 + EnumDescriptionChars() + 1 + EnumDescriptionChars() + 1;
  return count * (36u + chars * 2u);
}

void UnixToFileTime(int64_t unix_sec, uint32_t* low, uint32_t* high) {
  if (unix_sec < 0) unix_sec = 0;
  const uint64_t ticks = (uint64_t(unix_sec) + 11644473600ull) * 10000000ull;
  if (low) *low = uint32_t(ticks);
  if (high) *high = uint32_t(ticks >> 32);
}

std::string DefaultPath() {
  std::string root;
  if (const char* over = std::getenv("GTAIV_ACHIEVEMENTS_DIR"); over && *over) {
    root = over;
  } else if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
    root = std::string(xdg) + "/gtaiv";
  } else if (const char* home = std::getenv("HOME"); home && *home) {
#if defined(__APPLE__)
    root = std::string(home) + "/Library/Application Support/gtaiv";
#else
    root = std::string(home) + "/.local/share/gtaiv";
#endif
  } else {
    root = "/tmp/gtaiv";
  }
  return root + "/achievements.json";
}

bool Enabled() { return OptionOn("GTAIV_ACHIEVEMENTS", true); }

std::vector<EnumItem> CurrentUnlocked(uint32_t start, uint32_t count) {
  std::lock_guard<std::mutex> lock(g_mu);
  return UnlockedSlice(Store::Load(DefaultPath()), start, count);
}

bool RecordUnlock(uint32_t id) {
  if (!Enabled() || id == 0) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  Store store = Store::Load(DefaultPath());
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  if (!store.Add(id, now)) return false;
  if (!store.Save()) {
    LogLine("achievement: could not write %s\n", store.path.c_str());
    return false;
  }
  const Entry* known = Find(id);
  const std::string name = known ? known->name : "Achievement " + std::to_string(id);
  const std::string description = known ? known->description : "";
  const int points = known ? known->points : -1;
  const PendingToast item = MakeToast(CurrentStyle(), points, name, description, false);
  EnqueueLocked(item, SteadyMs());
  LogLine("achievement: unlocked %u %s\n", id, item.line2.c_str());
  return true;
}

void QueueVehicleToast(const std::string& vehicle_name) {
  std::lock_guard<std::mutex> lock(g_mu);
  const PendingToast item = MakeToast(CurrentStyle(), -1, vehicle_name, "", true);
  EnqueueLocked(item, SteadyMs());
  LogLine("achievement: vehicle toast %s (not saved)\n", item.line2.c_str());
}

void ToastQueue::Push(std::string headline, std::string detail, int64_t now_ms) {
  Push(Style::XboxA, -1, std::move(headline), std::move(detail), {}, now_ms);
}

void ToastQueue::Push(Style style, int points, std::string line1, std::string line2, std::string line3, int64_t now_ms) {
  Item item;
  item.style = style;
  item.points = points;
  item.line1 = std::move(line1);
  item.line2 = std::move(line2);
  item.line3 = std::move(line3);
  if (q_.empty()) item.start_ms = now_ms;
  q_.push_back(std::move(item));
}

ToastFrame ToastQueue::At(int64_t now_ms) {
  ToastFrame frame;
  while (!q_.empty()) {
    Item& item = q_.front();
    if (item.start_ms == 0) item.start_ms = now_ms;
    if (now_ms < item.start_ms) now_ms = item.start_ms;
    const int64_t elapsed = now_ms - item.start_ms;
    if (elapsed >= StyleDuration(item.style)) {
      q_.erase(q_.begin());
      if (!q_.empty() && q_.front().start_ms == 0) q_.front().start_ms = now_ms;
      continue;
    }
    const Motion motion = MotionAt(item.style, elapsed);
    if (!motion.visible) {
      q_.erase(q_.begin());
      continue;
    }
    frame.visible = true;
    frame.slide = motion.slide;
    frame.orb = motion.orb;
    frame.pill = motion.pill;
    frame.fade = motion.fade;
    frame.style = item.style;
    frame.anchor = StyleAnchor(item.style);
    frame.points = item.points;
    frame.headline = item.line1;
    frame.detail = item.line2;
    frame.line3 = item.line3;
    if (!item.chimed) {
      item.chimed = true;
      frame.chime = true;
    }
    return frame;
  }
  return frame;
}

void ToastQueue::Clear() { q_.clear(); }

ToastFrame PresentToastAt(bool scene, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (scene && !g_saw_scene) {
    g_saw_scene = true;
    g_scene_ms = now_ms;
    LogLine("achievements: first scene, toasts in %lld s\n", static_cast<long long>(kAfterSceneMs / 1000));
  }
  if (g_saw_scene && !g_gate_open && now_ms - g_scene_ms >= kAfterSceneMs) OpenGateLocked(now_ms);
  if (!g_gate_open) return {};
  return g_queue.At(now_ms);
}

Pcm MakeUnlockChime() {
  // A rising fifth (C5, then G5) with a short decay. Original; the console
  // sting is not used.
  constexpr int kRate = 44100;
  constexpr double kSeconds = 0.48;
  const int n = int(kRate * kSeconds);
  Pcm pcm;
  pcm.sample_rate = kRate;
  pcm.samples.resize(size_t(n));
  constexpr double kPi = std::numbers::pi;
  for (int i = 0; i < n; i++) {
    const double t = double(i) / kRate;
    const double attack = std::min(1.0, t / 0.01);
    const double release = t > kSeconds - 0.03 ? std::max(0.0, (kSeconds - t) / 0.03) : 1.0;
    const double env = std::exp(-3.4 * t) * attack * release;
    const double a = std::sin(2.0 * kPi * 523.25 * t);
    const double b = std::sin(2.0 * kPi * 783.99 * t);
    const double mix = t < 0.14 ? 0.0 : t > 0.20 ? 1.0 : (t - 0.14) / 0.06;
    const int sample = int(std::lround((a * (1.0 - mix) + b * mix) * env * 9000.0));
    pcm.samples[size_t(i)] = int16_t(std::clamp(sample, -32767, 32767));
  }
  return pcm;
}

void ResetSessionForTests() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_queue.Clear();
  g_waiting.clear();
  g_test_armed = false;
  g_saw_scene = false;
  g_scene_ms = 0;
  g_gate_open = false;
}

}  // namespace gtaiv::achievements
