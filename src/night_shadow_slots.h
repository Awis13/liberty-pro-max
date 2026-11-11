// The night shadows' slots held steady (GTAIV_NIGHT_SHADOW_STABLE, on by
// default; shadows_lights.cpp hooks it in).
//
// Each frame 008C4550 ranks the shadow-casting lights (flags 2 or 4) by
// their distance and gives the nearest ones the shadow maps' slots (the
// table at 01225A00: 8 entries of 0x110, the light's id at +0xF8); a light
// without a slot is drawn unshadowed (008C3370 answers -1). The game
// measures that distance from a point 8 m in front of the camera
// ([vp+0x80] + [vp+0x70] * -8.0, the constant at 00F3E174): turning the
// camera moves the point by up to 16 m, the ranking churns, and the street
// lamp beside the player loses its shadow in some frames while it still
// shines (GitHub issue #50). Two changes:
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

}  // namespace night_shadow_slots
