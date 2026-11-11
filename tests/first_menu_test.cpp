// first_menu (src/first_menu.h): the state the game is given where it
// settles in a menu, with "Continue from the last save" and the episodes.

#include "first_menu.h"

#include <cstdio>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "first_menu_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

using first_menu::State;
constexpr uint32_t kMenu = first_menu::kStayInMenu, kLoad = first_menu::kLoadLastSave;

// The game's own values: 0 keeps the menu, 6 is the title menu's Start.
void TestValues() {
  EXPECT(kMenu == 0);
  EXPECT(kLoad == 6);
}

// Without Continue every menu stays, save or not.
void TestNoContinue() {
  for (bool reached : {false, true})
    for (bool chooser : {false, true})
      for (bool save : {false, true}) EXPECT(State(false, reached, chooser, save) == kMenu);
}

// IV alone: Continue goes on at the first menu, only there, as it always
// did (with no save the game starts a new one, as Start does).
void TestContinueWithoutEpisodes() {
  EXPECT(State(true, false, false, true) == kLoad);
  EXPECT(State(true, false, false, false) == kLoad);
  EXPECT(State(true, true, false, true) == kMenu);
  EXPECT(State(true, true, false, false) == kMenu);
}

// The episodes found: with a save, Continue goes past the chooser (the
// newest save, in its own game); with none, the chooser stays.
void TestContinueWithTheChooser() {
  EXPECT(State(true, false, true, true) == kLoad);
  EXPECT(State(true, false, true, false) == kMenu);
}

// After the chooser (the picked game's own menu), Continue never applies.
void TestAfterThePick() {
  EXPECT(State(true, true, false, true) == kMenu);
  EXPECT(State(true, true, false, false) == kMenu);
  EXPECT(State(true, true, true, true) == kMenu);
  EXPECT(State(true, true, true, false) == kMenu);
}

void TestSaveNames() {
  using first_menu::IsSaveName;
  EXPECT(IsSaveName("SGTA400"));
  EXPECT(IsSaveName("SGTA412"));
  EXPECT(IsSaveName("sgta405"));
  EXPECT(!IsSaveName("SGTA4"));
  EXPECT(!IsSaveName("SGTA4001"));
  EXPECT(!IsSaveName("SGTA40a"));
  EXPECT(!IsSaveName("SGTA411.json"));
  EXPECT(!IsSaveName("ControlMap.dat"));
  EXPECT(!IsSaveName("SGTA300"));
}

// An empty file with a save's name (a copy cut short) is no save.
void TestSaveFiles() {
  using first_menu::IsSave;
  EXPECT(IsSave("SGTA400", 1977934));
  EXPECT(IsSave("SGTA401", 1));
  EXPECT(!IsSave("SGTA400", 0));
  EXPECT(!IsSave("ControlMap.dat", 756));
}

// The game a start takes. With the episodes off: IV, whatever the command
// line says; its own -episode 0 is the game's own.
void TestStartEpisodesOff() {
  using first_menu::ChooseStart;
  using first_menu::StartRule;
  EXPECT(ChooseStart(false, false, -1, false, false) == StartRule::kIV);
  EXPECT(ChooseStart(false, false, -1, true, true) == StartRule::kIV);
  EXPECT(ChooseStart(false, true, 0, true, true) == StartRule::kGameOwn);
  EXPECT(ChooseStart(false, true, 1, true, true) == StartRule::kIVArgIgnored);
  EXPECT(ChooseStart(false, true, 2, false, false) == StartRule::kIVArgIgnored);
  EXPECT(ChooseStart(false, true, 7, true, true) == StartRule::kIVArgIgnored);
}

// With the episodes on: a player's -episode is kept; without one, the
// game's own with both episodes there, otherwise the newest save of a
// game that is there.
void TestStartEpisodesOn() {
  using first_menu::ChooseStart;
  using first_menu::StartRule;
  EXPECT(ChooseStart(true, false, -1, true, true) == StartRule::kGameOwn);
  EXPECT(ChooseStart(true, false, -1, true, false) == StartRule::kNewestThere);
  EXPECT(ChooseStart(true, false, -1, false, true) == StartRule::kNewestThere);
  EXPECT(ChooseStart(true, false, -1, false, false) == StartRule::kNewestThere);
  EXPECT(ChooseStart(true, true, 2, true, false) == StartRule::kGameOwn);
  EXPECT(ChooseStart(true, true, 0, false, false) == StartRule::kGameOwn);
}

void TestSaveTimes() {
  using first_menu::Newer;
  using first_menu::SaveTime;
  constexpr SaveTime a{{2026, 11, 10, 20, 15, 3}};
  constexpr SaveTime b{{2026, 11, 10, 20, 15, 4}};
  constexpr SaveTime c{{2025, 12, 31, 23, 59, 59}};
  EXPECT(Newer(b, a));
  EXPECT(!Newer(a, b));
  EXPECT(!Newer(a, a));
  EXPECT(Newer(a, c));
  EXPECT(!Newer(c, a));
}

// IV 0, TLAD 1, TBoGT 2: the newest save of a game that is there.
void TestNewestThere() {
  using first_menu::NewestThere;
  using first_menu::SaveTime;
  constexpr SaveTime older{{2026, 1, 1, 10, 0, 0}};
  constexpr SaveTime newer{{2026, 1, 2, 10, 0, 0}};
  constexpr SaveTime newest{{2026, 3, 1, 10, 0, 0}};
  constexpr SaveTime none{};
  // TBoGT's is the newest but its folder is missing: TLAD's (newer than IV's).
  EXPECT(NewestThere({older, newer, newest}, {0, 1, 2}, {true, true, false}) == 1);
  // Only IV there: IV's, though both episodes' are newer.
  EXPECT(NewestThere({older, newer, newest}, {0, 1, 2}, {true, false, false}) == 0);
  // All there: the newest overall, as the game itself.
  EXPECT(NewestThere({older, newer, newest}, {0, 1, 2}, {true, true, true}) == 2);
  // No IV save, the only episode save's episode missing: IV, a new game.
  EXPECT(NewestThere({none, none, newest}, {-1, -1, 4}, {true, true, false}) == 0);
  // A game with no save is never taken over one with a save.
  EXPECT(NewestThere({none, older, none}, {-1, 3, -1}, {true, true, true}) == 1);
  // A tie: the first.
  EXPECT(NewestThere({newer, newer, older}, {0, 1, 2}, {true, true, true}) == 0);
}

static_assert(first_menu::State(true, false, false, false) == 6, "constexpr");

}  // namespace

int main() {
  TestValues();
  TestNoContinue();
  TestContinueWithoutEpisodes();
  TestContinueWithTheChooser();
  TestAfterThePick();
  TestSaveNames();
  TestSaveFiles();
  TestStartEpisodesOff();
  TestStartEpisodesOn();
  TestSaveTimes();
  TestNewestThere();
  if (g_failures) {
    std::fprintf(stderr, "first_menu_test: %d failed\n", g_failures);
    return 1;
  }
  std::printf("first_menu_test: all passed\n");
  return 0;
}
