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

// Without Continue every menu stays.
void TestNoContinue() {
  EXPECT(State(false, false, false) == kMenu);
  EXPECT(State(false, false, true) == kMenu);
  EXPECT(State(false, true, false) == kMenu);
  EXPECT(State(false, true, true) == kMenu);
}

// IV alone: Continue loads the last save at the first menu, only there.
void TestContinueWithoutEpisodes() {
  EXPECT(State(true, false, false) == kLoad);
  EXPECT(State(true, true, false) == kMenu);
}

// The episodes found: the chooser stays, and the menu of the game picked
// after it too (the first menu was the chooser).
void TestContinueWithTheChooser() {
  EXPECT(State(true, false, true) == kMenu);
  EXPECT(State(true, true, false) == kMenu);
  EXPECT(State(true, true, true) == kMenu);
}

static_assert(first_menu::State(true, false, false) == 6, "constexpr");

}  // namespace

int main() {
  TestValues();
  TestNoContinue();
  TestContinueWithoutEpisodes();
  TestContinueWithTheChooser();
  if (g_failures) {
    std::fprintf(stderr, "first_menu_test: %d failed\n", g_failures);
    return 1;
  }
  std::printf("first_menu_test: all passed\n");
  return 0;
}
