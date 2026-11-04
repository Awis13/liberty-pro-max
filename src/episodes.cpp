// The episodes, The Lost and Damned and The Ballad of Gay Tony, from their
// folders next to the game's (TLAD, TBoGT). The game looks for them in the
// registry (0088F080) after Live's list of downloaded content, and with one
// found opens with its own screen to choose what to play.
//
//   GTAIV_EPISODES: the game finds the episodes it has the folders of.
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

}  // namespace recomp::gen
