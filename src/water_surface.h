// The water surface: the sizes GTAIV_WATER_TILING gives the game in place
// of what it works out from the water quality setting, and, with the
// option or without it, the surface's two render targets created at
// exactly the size the game asks for. What the hooks decide is decided
// here (the sizes' hooks are in community_fixes.cpp, the creation's in
// fixes.cpp): a hook passes in its register and, where the game's memory
// is read, what reads it, and tests/water_surface_test.cpp calls the same
// functions with its own. Plain C++, nothing of the runtime's.
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
//
// The same holds for a size the game did not ask for. The runtime can
// make render targets of a listed size larger (RECOMP_RT_BOOST; the
// launcher's high reflections list 512x512 and 256x256, its high shadows
// 1024x1024), which makes a reflection finer and breaks the simulation:
// the pass still offsets by half a texel of the size the game asked for.
// So the two targets are made at exactly that size, with the option or
// without it: created inside the runtime's scope of exact render targets.
//
// The game does not create a render target's Direct3D texture where it
// sets the target up (00A0DAAC and 00A0DACB only record the name and the
// size, 004239E0) but in 00423BA0 (esi: the target, a pointer to its name
// at +30), called when the target is first bound (00423E70, from
// 00421BB0) and for every target when the device comes back (00421FF0),
// on whichever thread that is. So 00423BA0 itself is wrapped
// (CreateTargetOf), which covers every caller, and the two are told by
// their names.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "guest_string.h"

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

// Whether a render target of this name (the game's own, as it gives them
// at 00A0DAA7 and 00A0DAC6) is one of the simulation's two. The water
// reflection's targets and the reflection map's are pictures: not these.
constexpr bool SimulationTarget(std::string_view name) {
  return name == "WATER_SURFACE0_COLOUR" || name == "WATER_SURFACE1_COLOUR";
}

// A render target's name out of the game's memory: the C string at va,
// byte(address) reading one byte of it. Nothing for a null pointer, which
// is not read; a name is cut at kLongestName, well past the ones compared
// here.
constexpr uint32_t kLongestName = 63;
template <class Byte>
std::string TargetName(uint32_t va, Byte&& byte) {
  return va ? guest_string::Read(va, kLongestName, byte) : std::string();
}

// The game creates the Direct3D texture or surface of the render target
// of this name; create() is that, called once. The simulation's two are
// created while a Scope lives, which ends with the creation however it
// ends; the others with none. Scope: the runtime's
// recomp::gfx::ExactTargetScope (gfx/target_size.h) from the hook, a
// recording one from the test.
template <class Scope, class Create>
void CreateTarget(std::string_view name, Create&& create) {
  if (!SimulationTarget(name)) {
    create();
    return;
  }
  const Scope exact;
  create();
}

// A render target object's pointer to its name (a C string, or null).
constexpr uint32_t kTargetNamePointer = 0x30;

// 00423BA0, all of what the port's function does. target: the render
// target object's address (esi); byte(address) and dword(address) read
// the game's memory; create() is the game's own 00423BA0.
template <class Scope, class Byte, class Dword, class Create>
void CreateTargetOf(uint32_t target, Byte&& byte, Dword&& dword, Create&& create) {
  CreateTarget<Scope>(TargetName(dword(target + kTargetNamePointer), byte), create);
}

}  // namespace water_surface
