// The timecycle's colour filter, as GTAIV_COLOUR_FILTER=natural (the
// default) changes a line of timecyc.dat (community_fixes.cpp, the hook
// after its sscanf): the colour correction (columns 39-41, the scene's
// colour times CC * 2 / 255 in rage_postfx: the blue-green nights, the
// yellow evenings) neutral, 128. Nothing else: the desaturation (45, 48)
// and the gamma (47, 50) stay the game's. The gamma is the game's own lift
// of the darks (rage_postfx multiplies the colour by lum^(gamma - 1): at
// 0.8 by day, 1.58 times at a luminance of 0.1); set to 1 (2026-11-09 to
// 11-13) the darks crushed, and with the desaturation moved toward full
// colour the grass and leaves went acid (a park by day,
// 2026-11-13). gta4-rtx keeps that gamma too, as an exposure bias.

#pragma once

#include <cstdint>

namespace gtaiv_colour_filter {

inline constexpr uint32_t kColourCorrect = 39;  // columns 39, 40, 41 (%d)
inline constexpr uint32_t kColumns = 51;         // a line the filter applies to has at least these

struct Line {
  int32_t cc[3];
  float desaturation[2];  // near, far
  float gamma[2];
};

inline void Natural(Line* l) {
  for (int32_t& c : l->cc) c = 128;
}

}  // namespace gtaiv_colour_filter
