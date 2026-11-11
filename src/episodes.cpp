// The episodes, The Lost and Damned and The Ballad of Gay Tony, from their
// folders next to the game's (TLAD, TBoGT). The game looks for them in the
// registry (0088F080) after Live's list of downloaded content, and with one
// found opens with its own screen to choose what to play.
//
//   GTAIV_EPISODES (off by default): the game finds the episodes it has
//     the folders of.
//   GTAIV_EPISODE_RADIO_OWN: an episode's radio songs play only in that
//     episode, not in IV or the other episode (on by default).
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
void f_008905E0_lifted(Ctx& c);
void f_008906B0_lifted(Ctx& c);

namespace {

bool EpisodesOn() {
  static const bool on = OptionOn("GTAIV_EPISODES", false);
  return on;
}

// Each episode's setup2.xml lists two contents: the episode itself, with its
// number in <episode>, enabled while that episode is chosen (00890900), and
// its radio (e1_radio.xml, e2_radio.xml in <audiometadata>), with no
// <episode> and <enabled />: on whatever is played. Starting a game
// (00494140) loads every enabled content's audio lists, through 008913E0
// (00B70660) and, coming from IV, 00891440 (00493A00), one content at a time
// (008905E0, 008906B0), so that IV's stations also play both episodes' songs
// and each episode's the other's: as with the episodes downloaded on a
// console, not as in IV alone.
//
// With GTAIV_EPISODE_RADIO_OWN, a content with audio lists and no episode
// number is loaded only when the game being started is the episode of the
// same setup2.xml. That game is in [0112D788] by then; [0112D754], the one
// running, still holds the one before (it follows in 00493E20, after the
// loads).
constexpr gaddr kStartingEpisode = 0x0112D788;  // 0 IV, 1 TLAD, 2 TBoGT
// The content manager (this): its contents, 0x22C bytes each.
constexpr uint32_t kContents = 0x144, kContentCount = 0x148, kContentSize = 0x22C;
// A content: the index of its setup2.xml, <audiometadata>, whether it is in
// use, <episode>.
constexpr uint32_t kContentSetup = 0x0, kContentAudio = 0xA4, kContentUsed = 0x226, kContentEpisode = 0x229;

bool EpisodeRadioOwn() {
  static const bool on = OptionOn("GTAIV_EPISODE_RADIO_OWN", true);
  return on;
}

// Whether the content at this index is an episode's radio and the game
// being started is not that episode.
bool OtherGamesRadio(uint32_t manager, uint32_t index) {
  if (!EpisodeRadioOwn() || !EpisodesOn()) return false;
  const uint32_t count = Rd16(manager + kContentCount);
  const uint32_t contents = Rd32(manager + kContents);
  if (index >= count || !contents) return false;
  const uint32_t content = contents + index * kContentSize;
  if (!Rd8(content + kContentAudio) || Rd8(content + kContentEpisode)) return false;
  // The episode of the same setup2.xml.
  uint32_t episode = 0;
  for (uint32_t i = 0; i < count && !episode; i++) {
    const uint32_t other = contents + i * kContentSize;
    if (Rd8(other + kContentUsed) && Rd32(other + kContentSetup) == Rd32(content + kContentSetup))
      episode = Rd8(other + kContentEpisode);
  }
  const uint32_t starting = Rd32(kStartingEpisode);
  if (!episode || episode == starting) return false;
  WarnF("gtaiv: episodes: %s of episode %u left out of game %u\n",
        reinterpret_cast<const char*>(GPtr(content + kContentAudio)), episode, starting);
  return true;
}

}  // namespace

// thiscall (index), both: a content's archives, data file and audio lists
// loaded (008905E0), or its audio lists alone (008906B0).
void f_008905E0(Ctx& c) {
  if (!OtherGamesRadio(c.ecx, Rd32(c.esp + 4))) return f_008905E0_lifted(c);
  c.esp += 8;  // ret 4
}
void f_008906B0(Ctx& c) {
  if (!OtherGamesRadio(c.ecx, Rd32(c.esp + 4))) return f_008906B0_lifted(c);
  c.esp += 8;  // ret 4
}

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
