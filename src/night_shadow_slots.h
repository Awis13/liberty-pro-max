// The night shadows' slots held steady (GTAIV_NIGHT_SHADOW_STABLE, on by
// default; shadows_lights.cpp hooks it in).
//
// Every few frames 008C4550 ranks the shadow-casting lights (flags 2 or 4)
// and gives eight of them the shadow maps' dynamic slots (the table at
// 01225A00: 8 entries of 0x110, the light's id at +0xF8; slots 1 to 7 are
// drawn). A light without one is drawn with its static cached shadow if it
// has one (flags 2: 008C3370 answers 8 + its cache index), which holds only
// the buildings, no people or cars, or unshadowed (-1). The ranking puts the
// lights flagged 0x100 (the cars' lights) before all others whatever their
// distance (008C32B0), and measures the distance from a point 8 m in front of
// the camera ([vp+0x80] + [vp+0x70] * -8.0, the constant at 00F3E174). Turn
// the camera toward a street with cars and their lights take every slot: the
// street lamp beside the player falls back to its cached shadow, and the
// player's shadow, a lamp post's, vanish while the lamp still shines; turn
// back and they return (GitHub issue #50). Three changes:
//   - a car's light and another light compete on their distance (Priority;
//     0x400, the game's own forced lights, still first);
//   - the distance from the camera itself (the -8.0 read as 0 at that one
//     place: the constant is shared), so turning the camera alone changes
//     nothing;
//   - a light that held a slot in the frame before competes with its
//     distance times kHeldShare, so another must be clearly nearer to take
//     its slot (no flicker between two lamps at about the same distance).
// FusionFix does not fix this: it turns outdoor night shadows off.

#pragma once

#include <cstddef>
#include <cstdint>

namespace night_shadow_slots {

inline constexpr float kHeldShare = 0.6f;
inline constexpr uint32_t kSlotTable = 0x01225A00, kSlotStride = 0x110, kSlotId = 0xF8;
inline constexpr int kSlots = 8;

// Whether light `id` (nonzero) is among the slots' ids (the table's as the
// ranking starts: the frame before's).
inline bool Held(uint32_t id, const uint32_t (&ids)[kSlots]) {
  if (id == 0) return false;
  for (uint32_t s : ids)
    if (s == id) return true;
  return false;
}

// The distance a light competes with.
inline float RankDistance(float distance, bool held) { return held ? distance * kHeldShare : distance; }

// 008C32B0: how a light flagged `fresh` meets one flagged `held` already in
// a ranking place (0 when the place is empty): 0 it takes the place, 1 the
// distances decide, 2 it does not. The game's: a 0x400 light takes any place
// and none takes a 0x400 light's; a 0x100 light takes any other's place and
// another light never takes a 0x100 light's. even: 0x100 and other lights
// meet on their distances (1).
inline uint32_t Priority(uint32_t fresh, uint32_t held, bool even) {
  if (held == 0 || (fresh & 0x400)) return 0;
  if (held & 0x400) return 2;
  if (even) return 1;
  if (fresh & 0x100) return (held & 0x100) ? 1 : 0;
  return (held & 0x100) ? 2 : 1;
}

}  // namespace night_shadow_slots
