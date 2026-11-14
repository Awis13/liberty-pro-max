// An on/off option from the environment (the launcher sets each): "1" on,
// "0" off, unset its default.
#pragma once

#include <cstdint>
#include <cstdlib>

inline bool OptionOn(const char* name, bool def) {
  const char* e = std::getenv(name);
  if (!e || !*e) return def;
  return *e != '0';
}

namespace recomp::gen {
// The episodes' option, GTAIV_EPISODES, with its fallback: the one reader
// (src/episodes.cpp).
bool EpisodesOn();
// Whether an episode (1 TLAD, 2 TBoGT) is installed in its folder, so that
// the game takes it (src/episode_files.h; src/episodes.cpp).
bool EpisodeInstalled(uint32_t episode);
}  // namespace recomp::gen
