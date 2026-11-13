// The water surface's sizes: what GTAIV_WATER_TILING gives the game in
// place of what it works out from the water quality setting. The hooks
// in community_fixes.cpp call these functions and nothing else, and so
// does tests/water_surface_test.cpp. Plain C++.
//
// The surface (the ripples) is simulated in two square render targets,
// WATER_SURFACE0_COLOUR and WATER_SURFACE1_COLOUR, each frame's drawn
// from the other's by a full-target quad (009AD760). Four places take a
// size from the setting ([010FC324], 0-3), each as 128 << setting:
//   00A0D979  the targets' side, at most 1024; the water reflection's
//             targets are made first from the same register, the
//             surface's two from 00A0DA85 on;
//   009ADB46  the pass's texel: 256 / side for the shader, which samples
//             the neighbours at that over 256, and 0.5 / side added to
//             the quad's texture coordinates;
//   009AA8AA, 009AFC40  the noise texture's side (it is made in two
//             places).
// The pass is right only when its texel is the targets': one of a
// different size reads the last frame half a texel off its own grid, more
// each frame, and the neighbours several texels apart. The consoles have
// 256x256 (setting 1); the option gives all four that.
#pragma once

#include <cstdint>

namespace water_surface {

constexpr uint32_t kSmallestSide = 0x80;  // mov reg, 80: setting 0
constexpr uint32_t kConsoleSetting = 1;   // 128 << 1: 256x256

// mov reg, 80; shl reg, cl.
constexpr uint32_t Side(uint32_t setting) { return kSmallestSide << (setting & 31); }

// 00A0DA85: the side of the surface's two render targets, given the side
// the game worked out (esi). Without the option, that.
constexpr uint32_t TargetSide(bool tiling, uint32_t esi) { return tiling ? Side(kConsoleSetting) : esi; }

// 009ADB46: the setting the pass works its texel out from, given the
// game's setting. Without the option, that.
constexpr uint32_t PassSetting(bool tiling, uint32_t setting) { return tiling ? kConsoleSetting : setting; }

// 009AA8AA, 009AFC40: the setting the noise texture's side is worked out
// from.
constexpr uint32_t NoiseSetting(bool tiling, uint32_t setting) { return tiling ? kConsoleSetting : setting; }

}  // namespace water_surface
