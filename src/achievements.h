// Local achievements for the GFWL calls the game already makes.
//
// GTA IV 1.0.8.0 imports xlive ordinal 5278 (XUserWriteAchievements) and
// 5280 (XUserCreateAchievementEnumerator); see symbols/gtaiv/imports.tsv.
// The runtime's offline stand-in returns success from the write and throws
// the ids away. This file keeps them.
//
// Unlocks go to <data>/achievements.json (on a Mac,
// ~/Library/Application Support/gtaiv/achievements.json). Save files are
// not read or written. Ids already stored in a save from before this
// feature are not reconstructed: the game does not ask Live again, and
// there is no checked layout of those stats here.
//
// The ids are the script indices AWARD_ACHIEVEMENT takes. On this PC build
// those are what gets passed to XUserWriteAchievements. The table matches
// the public lists cited in achievements.cpp. An id that is not in the
// table is still stored and shown as "Achievement N".
//
// IV, The Lost and Damned and The Ballad of Gay Tony share one id space on
// PC (1-50, 51-55, 56-65), so one file holds all three, with a game tag on
// each unlock. The write call itself has no title id.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gtaiv::achievements {

enum class Game { iv, tlad, tbogt, unknown };

const char* GameKey(Game game);
Game GameFromKey(const std::string& key);

struct Entry {
  uint32_t id;
  Game game;
  int points;
  const char* name;
  const char* description;
};

// The public table, in id order. Null if `id` is not in it.
const Entry* Find(uint32_t id);
const Entry* Catalog();
int CatalogCount();

// "5G - Off The Boat", or just the name when points is negative (unknown).
std::string ToastDetail(int points, const std::string& name);

// Toast styles. GTAIV_ACHIEVEMENT_STYLE is one of these keys; unknown or
// unset is xbox_a.
enum class Style { XboxA, XboxB, XboxC, XboxD, XboxE, Ps3, Steam };
enum class Anchor { BottomCenter, TopRight, BottomRight };
// GTAIV_ACHIEVEMENT_POSITION. Default keeps each style's own corner.
enum class Place { Default, TopLeft, TopRight, BottomLeft, BottomRight, Top, Bottom };

Style ParseStyle(const char* text);
const char* StyleKey(Style style);
Style CurrentStyle();
Anchor StyleAnchor(Style style);

Place ParsePlace(const char* text);
const char* PlaceKey(Place place);
Place CurrentPlace();

// "SULTAN RS" becomes "Sultan RS". Words of one or two letters stay
// capitals (RS). Used for the vehicle toast.
std::string TitleCaseLabel(const std::string& text);

// The toast bitmap's first row is its top. Core Graphics already stores
// a context that way (a high y is the last row), and Metal shows the
// first uploaded row at the top of the quad. Reversing the rows is what
// drew the toast upside down. This copy keeps row 0 as row 0.
void CopyToastRows(const uint8_t* src, uint8_t* dst, int width, int height);

// How a toast of `style` looks `elapsed` ms after it started.
struct Motion {
  bool visible = false;
  float orb = 0;    // Xbox: the round mark, 0..1
  float pill = 0;   // Xbox: the capsule grown out from the mark, 0..1
  float slide = 0;  // 0 off screen, 1 resting (PlayStation and Steam)
  float fade = 0;
};
Motion MotionAt(Style style, int64_t elapsed_ms);
int64_t StyleDuration(Style style);

// Where the toast sits in a view, in pixels. Sizes are the 1280x720 spec
// scaled by view height / 720. With the position left at default, `slide`
// moves PlayStation off the right and Steam off the bottom, and an Xbox
// toast stays put (it grows in place). Any other position uses the same
// margin on every edge and slides in from that edge: top from above,
// bottom from below, a corner from its side.
struct ToastBox {
  int x = 0, y = 0, w = 0, h = 0;
};
ToastBox PlaceToast(Style style, float slide, int view_w, int view_h);

struct ToastCopy {
  std::string line1, line2, line3;
};
// Vehicle toasts say "Entered" / the name and are not an achievement.
ToastCopy CopyFor(Style style, int points, const std::string& name, const std::string& description, bool vehicle);

struct Unlock {
  uint32_t id = 0;
  Game game = Game::unknown;
  int64_t unlocked_at = 0;  // unix seconds, UTC
};

// One achievements.json. Load of a missing file is an empty store. A file
// that is not ours is renamed aside (.bak) and not overwritten.
class Store {
 public:
  std::string path;
  bool save_blocked = false;

  static Store Load(const std::string& path);
  bool Save() const;
  // False if it was already unlocked, the id is 0, or saving is blocked.
  bool Add(uint32_t id, int64_t unix_sec);
  void Clear();
  bool IsUnlocked(uint32_t id) const;
  const std::vector<Unlock>& unlocks() const { return unlocks_; }

 private:
  std::vector<Unlock> unlocks_;
};

struct Row {
  uint32_t id = 0;
  Game game = Game::unknown;
  std::string name;
  std::string description;
  int points = -1;  // negative: not in the table
  bool unlocked = false;
  int64_t unlocked_at = 0;
};

struct Score {
  int unlocked_known = 0;
  int catalog_count = 0;
  int unlocked_points = 0;
  int catalog_points = 0;
  int unlocked_unknown = 0;
};

// Catalog order, then any stored id the table does not know.
std::vector<Row> Rows(const Store& store);
Score Summarize(const Store& store);

// What XEnumerate can hand back: unlocked only, `start` entries into that
// list skipped, at most `count` of them. Sorted by id.
struct EnumItem {
  uint32_t id = 0;
  int points = 0;
  int64_t unlocked_at = 0;
  std::string label;
  std::string description;
};
std::vector<EnumItem> UnlockedSlice(const Store& store, uint32_t start, uint32_t count);
// The unlocked page in the on-disk list. For the Live enumerator.
std::vector<EnumItem> CurrentUnlocked(uint32_t start, uint32_t count);

// Bytes XUserCreateAchievementEnumerator should report so the strings fit
// in the caller's buffer after the 36-byte XACHIEVEMENT_DETAILS rows.
uint32_t EnumBufferBytes(uint32_t count);
// Caps for the wide strings packed into that buffer.
uint32_t EnumLabelChars();
uint32_t EnumDescriptionChars();

// FILETIME (100 ns since 1601) for a unix second, split for a 32-bit guest.
void UnixToFileTime(int64_t unix_sec, uint32_t* low, uint32_t* high);

// <data dir>/achievements.json. GTAIV_ACHIEVEMENTS_DIR overrides the directory.
// Same directory the runtime uses for this game (DataDir): on a Mac,
// ~/Library/Application Support/gtaiv.
std::string DefaultPath();

// GTAIV_ACHIEVEMENTS, on when unset. The launcher sets it.
bool Enabled();

// The in-game side. RecordUnlock persists a new id and queues its toast.
// Safe to call from the game thread while CurrentToast runs on the encode
// thread. Disabled, or an id already stored: no write and no toast.
bool RecordUnlock(uint32_t id);

// Slide in, hold, slide out. About five seconds in all.
constexpr int64_t kToastSlideMs = 400;
constexpr int64_t kToastHoldMs = 4200;
constexpr int64_t kToastTotalMs = kToastSlideMs + kToastHoldMs + kToastSlideMs;
// Loading screens present for a long time before the city is on screen
// (the renderer's "first scene"). Toasts wait this long after that, so
// one earned or tested during the load is still there to see.
constexpr int64_t kAfterSceneMs = 3000;

struct ToastFrame {
  bool visible = false;
  float slide = 0;  // 0 off screen, 1 resting
  float orb = 0;
  float pill = 0;
  float fade = 0;
  bool chime = false;
  Style style = Style::XboxA;
  Anchor anchor = Anchor::BottomCenter;
  int points = -1;
  std::string headline;  // line1
  std::string detail;    // line2
  std::string line3;
};

class ToastQueue {
 public:
  void Push(std::string headline, std::string detail, int64_t now_ms);
  void Push(Style style, int points, std::string line1, std::string line2, std::string line3, int64_t now_ms);
  ToastFrame At(int64_t now_ms);
  void Clear();

 private:
  struct Item {
    std::string line1, line2, line3;
    Style style = Style::XboxA;
    int points = -1;
    int64_t start_ms = 0;
    bool chimed = false;
  };
  std::vector<Item> q_;
};

// The toast to draw for this present. `scene` is true once the renderer
// has shown a first scene. Nothing is drawn until kAfterSceneMs after
// that, including a GTAIV_ACHIEVEMENT_TEST toast (armed then, not during
// the load). `now_ms` is a steady clock. Lines are fprintf(stderr), the
// same stream as "gfx: first scene", flushed so they land in the game log.
ToastFrame PresentToastAt(bool scene, int64_t now_ms);

struct Pcm {
  int sample_rate = 44100;
  std::vector<std::int16_t> samples;
};
// A short original two-note chime. Not the console's sound.
Pcm MakeUnlockChime();

// A vehicle-entry preview. Not written to achievements.json.
void QueueVehicleToast(const std::string& vehicle_name);

// Drops the queued toast and lets the test env arm again. For unit tests.
void ResetSessionForTests();

}  // namespace gtaiv::achievements
