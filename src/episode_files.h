// Whether an episode (1 The Lost and Damned, 2 The Ballad of Gay Tony) is
// installed in its folder next to the game's, as the game decides it.
// Plain C++: tests/episode_files_test.cpp checks it.
//
// The game takes an episode's folder (0088F440, from 0088FF10: mask 2 TLAD,
// 4 TBoGT) only if it can open the list of the episode's files in it,
// <folder>\E1_List.txt for TLAD and <folder>\E2_List.txt for TBoGT
// (008570B0, which opens it to read through 00856540 and answers 0 when it
// cannot). Only then is the folder kept (0088FE00), its "installed" flag
// set ([018CAF3C] TLAD, [018CAF3D] TBoGT, which the chooser reads) and its
// setup2.xml read (00890B10, from 0088FBB0), which names the episode's
// contents. A folder without the list is as no folder at all: no chooser,
// and nothing said. The Complete Edition's folders have the lists; an
// episode's files from elsewhere may not.
//
// So an episode is installed when its folder has both files. The port
// tells apart the folder that has setup2.xml and no list, to say so in the
// log (episodes.cpp).
#pragma once

#include <cstdint>
#include <string>

namespace episode_files {

constexpr uint32_t kTLAD = 1, kTBoGT = 2;

constexpr const char kSetup[] = "setup2.xml";

// An episode's folder in the game's, nullptr for no episode.
constexpr const char* Folder(uint32_t episode) {
  return episode == kTLAD ? "TLAD" : episode == kTBoGT ? "TBoGT" : nullptr;
}

// The list of an episode's files, in its folder: each episode its own.
constexpr const char* ListName(uint32_t episode) {
  return episode == kTLAD ? "E1_List.txt" : episode == kTBoGT ? "E2_List.txt" : nullptr;
}

// The episode of the game's mask (0088F440: bit 1 TLAD, tested first, bit
// 2 TBoGT), 0 for neither.
constexpr uint32_t EpisodeOfMask(uint32_t mask) {
  return mask & 2 ? kTLAD : mask & 4 ? kTBoGT : 0;
}

enum class State {
  kAbsent,     // no setup2.xml in its folder (or no such episode)
  kNoList,     // setup2.xml and no list: the game will not take it
  kInstalled,  // both
};

// has_file(path): whether a file below the game's folder is there, the
// path with backslashes ("TLAD\setup2.xml"). Names are given as the game
// spells them; matching them without case, as the game's files are found,
// is has_file's.
template <class HasFile>
State Find(uint32_t episode, HasFile&& has_file) {
  const char* folder = Folder(episode);
  if (!folder) return State::kAbsent;
  const std::string dir = std::string(folder) + "\\";
  if (!has_file(dir + kSetup)) return State::kAbsent;
  return has_file(dir + ListName(episode)) ? State::kInstalled : State::kNoList;
}

template <class HasFile>
bool Installed(uint32_t episode, HasFile&& has_file) {
  return Find(episode, has_file) == State::kInstalled;
}

}  // namespace episode_files
