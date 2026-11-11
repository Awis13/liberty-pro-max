// The frontend state the game is given where it settles in a menu
// (hook_0047D255 in options.cpp). Plain C++: tests/first_menu_test.cpp
// checks it.
//
// The frontend (0047C2C0, once a frame) ends many of its requests at
// 0047D255, push 0 to its state setter 007BDD80: 0 keeps the menu that is
// up on screen. GTAIV_CONTINUE gives 6 there the first time instead, what
// the title menu's Start gives (008D42D0): the frontend loop (00494350)
// stops and 004944E0 runs (from 004904BE): it scans the saves (0085C390,
// 0085A100 per slot), loads the newest and starts the game it was made in
// (0085A2E0 puts that save's game in [011D5E34], which 00494140 starts:
// 004946A1). With no save at all the scan ends with none (0085C390 case
// 2) and a new game starts in [011D5E34], the game being started
// ([0112D788], IV before any pick). With the episodes off, Continue takes
// IV's newest save instead, or a new IV game (f_0085A520 in options.cpp).
//
// With the episodes the first menu is the game's chooser (IV or the
// episodes), built at 0047C888 with [010FBBB1] set. A 6 there goes past it:
// with a save, that is Continue (the newest save, in its own game, IV's
// or an episode's); with none, a new IV game would start and the episodes
// would never be offered. So the chooser stays only when there is no save
// yet. The scan has not run when the first menu comes up (it runs after
// the frontend loop), so whether a save is there is read from the save
// folder (options.cpp). A player who wants another game than the newest
// save's turns Continue off.
//
// Continue does not apply after the chooser either: the pick (00D45AD0)
// builds the chosen game's title menu and comes back here, and its own
// Start goes on from there; a 6 from here would not wait for it.
#pragma once

#include <cstdint>
#include <string_view>

namespace first_menu {

constexpr uint32_t kStayInMenu = 0;  // the menu up stays
constexpr uint32_t kLoadLastSave = 6;  // as the title menu's Start

// cont: GTAIV_CONTINUE; reached: a menu has come up before; chooser: the
// menu up is the episode chooser; has_save: the player has a save.
constexpr uint32_t State(bool cont, bool reached, bool chooser, bool has_save) {
  return cont && !reached && (!chooser || has_save) ? kLoadLastSave : kStayInMenu;
}

// A save file's name: SGTA4 and two digits (the slot), in any case.
constexpr bool IsSaveName(std::string_view name) {
  if (name.size() != 7) return false;
  constexpr std::string_view kPrefix = "sgta4";
  for (size_t i = 0; i < kPrefix.size(); i++) {
    char ch = name[i];
    if (ch >= 'A' && ch <= 'Z') ch = char(ch + 0x20);
    if (ch != kPrefix[i]) return false;
  }
  return name[5] >= '0' && name[5] <= '9' && name[6] >= '0' && name[6] <= '9';
}

// A file that counts as a save: a save's name and something in it. The
// game's scan (0085A100) takes any file it can open with a time
// (0061F1A0), reading its first 110h bytes for the slot's name (0061E870)
// without a check of what it read, so there is no test of its own to
// follow; but an empty file, as a copy cut short leaves, is no save the
// game can load.
constexpr bool IsSave(std::string_view name, std::uintmax_t size) {
  return IsSaveName(name) && size > 0;
}

}  // namespace first_menu
