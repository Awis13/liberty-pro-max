// The frontend state the game is given where it settles in a menu
// (hook_0047D255 in options.cpp). Plain C++: tests/first_menu_test.cpp
// checks it.
//
// The frontend (0047C2C0, once a frame) ends many of its requests at
// 0047D255, push 0 to its state setter 007BDD80: 0 keeps the menu that is
// up on screen. GTAIV_CONTINUE gives 6 there the first time instead, what
// the title menu's Start gives (008D42D0): the frontend loop (00494350)
// stops and the last save loads.
//
// With the episodes the first menu is the game's chooser (IV or the
// episodes), built at 0047C888 with [010FBBB1] set. 6 there starts IV at
// once, the chooser never seen. So the chooser always stays, and Continue
// does not apply after the pick either: the pick (00D45AD0) builds the
// chosen game's title menu and comes back here, but only that menu's Start
// sets the game to start ([0112D788], from the pick) before its 6; a 6 from
// here would start IV whichever game was picked.
#pragma once

#include <cstdint>

namespace first_menu {

constexpr uint32_t kStayInMenu = 0;  // the menu up stays
constexpr uint32_t kLoadLastSave = 6;  // as the title menu's Start

// cont: GTAIV_CONTINUE; reached: a menu has come up before; chooser: the
// menu up is the episode chooser.
constexpr uint32_t State(bool cont, bool reached, bool chooser) {
  return cont && !reached && !chooser ? kLoadLastSave : kStayInMenu;
}

}  // namespace first_menu
