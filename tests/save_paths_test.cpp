// save_paths (src/save_paths.h): which folders in the saves' folder are
// profiles' folders, and which one the game reads.

#include "save_paths.h"

#include <cstdio>
#include <string_view>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "save_paths_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

// The syncs copy every profile's folder: any name that starts with user_.
void TestProfileFolders() {
  using save_paths::IsProfileFolder;
  EXPECT(IsProfileFolder("user_ee000000deadc0de"));
  EXPECT(IsProfileFolder("user_0123456789abcdef"));
  EXPECT(IsProfileFolder("user_x 2"));  // iCloud's rename of a clash
  EXPECT(IsProfileFolder("user_"));
  EXPECT(!IsProfileFolder("User_ee000000deadc0de"));
  EXPECT(!IsProfileFolder("user"));
  EXPECT(!IsProfileFolder(""));
  EXPECT(!IsProfileFolder(".DS_Store"));
}

// The check for a save before the first menu: only the folder the game
// reads, in any case.
void TestGameProfileFolder() {
  using save_paths::IsGameProfileFolder;
  EXPECT(IsGameProfileFolder("user_ee000000deadc0de"));
  EXPECT(IsGameProfileFolder("user_EE000000DEADC0DE"));
  EXPECT(IsGameProfileFolder("USER_ee000000DeadC0de"));
  EXPECT(!IsGameProfileFolder("user_ee000000deadc0df"));
  EXPECT(!IsGameProfileFolder("user_ee000000deadc0de 2"));
  EXPECT(!IsGameProfileFolder("user_0123456789abcdef"));
  EXPECT(!IsGameProfileFolder(""));
}

// The folder ends in the game's savegames, under the guest's C: drive.
void TestSavesFolder() {
  constexpr std::string_view rel = save_paths::kSavesRel;
  EXPECT(rel.substr(0, 8) == "drive_c/");
  EXPECT(rel.size() > 10 && rel.substr(rel.size() - 10) == "/savegames");
}

static_assert(save_paths::IsGameProfileFolder(save_paths::kGameProfile), "constexpr");
static_assert(save_paths::IsProfileFolder(save_paths::kGameProfile), "the game's is a profile's");

}  // namespace

int main() {
  TestProfileFolders();
  TestGameProfileFolder();
  TestSavesFolder();
  if (g_failures) {
    std::fprintf(stderr, "save_paths_test: %d failed\n", g_failures);
    return 1;
  }
  std::printf("save_paths_test: all passed\n");
  return 0;
}
