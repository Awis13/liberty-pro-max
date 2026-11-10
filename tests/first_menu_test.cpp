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

static_assert(first_menu::State(true, false, false, false) == 6, "constexpr");

}  // namespace

int main() {
  TestValues();
  TestNoContinue();
  TestContinueWithoutEpisodes();
  TestContinueWithTheChooser();
  TestAfterThePick();
  TestSaveNames();
  if (g_failures) {
    std::fprintf(stderr, "first_menu_test: %d failed\n", g_failures);
    return 1;
  }
  std::printf("first_menu_test: all passed\n");
  return 0;
}
