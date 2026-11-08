// The episodes, The Lost and Damned and The Ballad of Gay Tony, from their
// folders next to the game's (TLAD, TBoGT). The game looks for them in the
// registry (0088F080) after Live's list of downloaded content, and with one
// found opens with its own screen to choose what to play.
//
//   GTAIV_EPISODES: the game finds the episodes it has the folders of.
//   Always on, IV too: native 2C89A1F9, which the Complete Edition scripts
//     call, registered and answering 0.
#include <cstring>
#include <filesystem>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

void f_0088F080_lifted(Ctx& c);
void f_0085A5F0_lifted(Ctx& c);
void f_008AE060_lifted(Ctx& c);

namespace {

bool EpisodesOn() {
  static const bool on = OptionOn("GTAIV_EPISODES", true);
  return on;
}

}  // namespace

// thiscall (mask, out): the install folder of the episode in the mask (2
// TLAD, 4 TBoGT) if it is there; al: whether it is.
void f_0088F080(Ctx& c) {
  if (!EpisodesOn()) return f_0088F080_lifted(c);
  const uint32_t mask = Rd32(c.esp + 4);
  const uint32_t out = Rd32(c.esp + 8);
  const char* folder = mask & 2 ? "TLAD" : mask & 4 ? "TBoGT" : nullptr;
  std::error_code ec;
  const bool there =
      folder && std::filesystem::exists(std::filesystem::path(g_proc->game_dir) / folder / "setup2.xml", ec);
  if (there) {
    const std::string dir = Game().guest_dir + "\\" + folder;
    for (size_t i = 0; i <= dir.size(); i++) Wr8(out + uint32_t(i), uint8_t(dir[i]));
    WarnF("gtaiv: episodes: %s\n", dir.c_str());
  }
  c.eax = (c.eax & ~0xFFu) | (there ? 1u : 0u);
  c.esp += 12;  // ret 8
}

// The highest episode new to the player's profile, for "has been downloaded,
// do you want to play it?". Nothing is downloaded here, and the question
// stops a save from loading at the start: none is new.
void f_0085A5F0(Ctx& c) {
  if (!EpisodesOn()) return f_0085A5F0_lifted(c);
  c.eax = 0xFFFFFFFFu;
  c.esp += 4;  // ret
}

// The Complete Edition scripts ask native 2C89A1F9 (no arguments, one bool)
// whether multiplayer is there: the phone scripts (IV's and the episodes')
// to decide whether the phone lists Multiplayer, and TLAD's mission billy2.
// 1.0.8.0 has no such native. Loading a script looks up every native it
// calls (006252D0, through 00626AF0), and one not found stops the game with
// "SCRT70" and 747217401, 2C89A1F9 in decimal: in the episodes as soon as
// the phone comes out, and when billy2 loads. 008AE060 registers most
// native groups (the core ones come before it, the HTML and console ones
// after it), once per game start, from 008B4460, before any script loads.
// After it, 00626B40 (stdcall hash, handler; al, whether it was added)
// registers this one with the game's handler that answers 0,
// IS_XBOX360_VERSION's, 00B0EA50. 0 is what a game without multiplayer
// answers. Always on, not only with GTAIV_EPISODES: IV's own Complete
// Edition scripts ask it too.
void f_008AE060(Ctx& c) {
  constexpr uint32_t kMultiplayerNative = 0x2C89A1F9;
  constexpr gaddr kIsXbox360Version = 0x00B0EA50;  // the handler that answers 0
  constexpr gaddr kRegisterNative = 0x00626B40;
  f_008AE060_lifted(c);
  if (!(CallGuest(c, kRegisterNative, {kMultiplayerNative, kIsXbox360Version}) & 0xFF))
    WarnF("gtaiv: natives: %08X not added (table full: scripts calling it stop with SCRT70; or already there)\n",
          kMultiplayerNative);
}

}  // namespace recomp::gen
