// Where the game's saves are, for the port's own code that looks at them:
// the check for a save before the first menu (options.cpp) and the saves'
// syncs (save_cloud.mm, save_sync_mac.mm). Plain C++: tests/save_paths_test.cpp
// checks it.
//
// The saves' folder is the game's (its Local Settings in the guest's C:
// drive), in the port's data folder (the runtime's DataDir). In it, one
// folder per profile, user_<XUID>, holds that profile's saves.
#pragma once

#include <string_view>

namespace save_paths {

// The saves' folder, relative to the port's data folder.
constexpr const char kSavesRel[] =
    "drive_c/Documents and Settings/Player/Local Settings/Application Data/Rockstar Games/GTA IV/savegames";

constexpr std::string_view kProfilePrefix = "user_";

// The profile folder the game reads: user_ and the XUID the runtime gives
// the signed-in profile (kXuid in vendor/x86recomp/src/runtime/hle/xlive.cpp),
// in lower case.
constexpr std::string_view kGameProfile = "user_ee000000deadc0de";

// Any profile's folder (the syncs copy them all): a name that starts with
// user_.
constexpr bool IsProfileFolder(std::string_view name) {
  return name.substr(0, kProfilePrefix.size()) == kProfilePrefix;
}

// The folder of the profile the game reads, in any case.
constexpr bool IsGameProfileFolder(std::string_view name) {
  if (name.size() != kGameProfile.size()) return false;
  for (size_t i = 0; i < name.size(); i++) {
    char ch = name[i];
    if (ch >= 'A' && ch <= 'Z') ch = char(ch + 0x20);
    if (ch != kGameProfile[i]) return false;
  }
  return true;
}

}  // namespace save_paths
