// water_surface (src/water_surface.h): what the water surface's hooks
// give the game. With the tiling option the surface's render targets,
// the pass that draws one from the other and the noise texture are all
// 256x256 at every setting; without it each hook gives back the game's
// own value.

#include "water_surface.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "water_surface_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

using water_surface::NoiseSetting;
using water_surface::PassSetting;
using water_surface::Side;
using water_surface::TargetSide;

// The game's own side for the targets (00A0D979: mov esi, 80; shl esi,
// cl; cmp esi, 400; jle), which is in esi when the hook runs.
uint32_t GamesTargetSide(uint32_t setting) {
  const uint32_t side = Side(setting);
  return side < 0x400 ? side : 0x400;
}

// What the game ends up with, each size from the function its hook
// calls and then as the game's code goes on from it.
struct Sizes {
  uint32_t target;   // the render targets' side
  uint32_t noise;    // the noise texture's side
  float tap;         // from a texel to the neighbours the pass samples
  float half_texel;  // added to the quad's texture coordinates
};
Sizes Of(bool tiling, uint32_t setting) {
  const float pass = float(Side(PassSetting(tiling, setting)));
  // The pass gives the shader 256 / side, which the shader takes over
  // 256, and adds 0.5 / side to the quad.
  return {TargetSide(tiling, GamesTargetSide(setting)), Side(NoiseSetting(tiling, setting)),
          256.0f / pass / 256.0f, 0.5f / pass};
}

// (Powers of two: the floats are exact.)
void TestWithTheOptionEveryHookGives256() {
  for (uint32_t setting = 0; setting <= 3; setting++) {
    EXPECT(TargetSide(true, GamesTargetSide(setting)) == 256);
    EXPECT(Side(PassSetting(true, setting)) == 256);
    EXPECT(Side(NoiseSetting(true, setting)) == 256);
  }
}

// The pass reads the last frame's target texel for texel: half a texel of
// the target on the quad, the neighbours one texel of it away, and noise
// of the same size.
void TestWithTheOptionThePassIsTheTargetsSize() {
  for (uint32_t setting = 0; setting <= 3; setting++) {
    const Sizes s = Of(true, setting);
    EXPECT(s.half_texel == 0.5f / float(s.target));
    EXPECT(s.tap * float(s.target) == 1.0f);
    EXPECT(s.noise == s.target);
  }
}

void TestWithoutTheOptionEveryHookGivesTheGamesOwn() {
  for (uint32_t setting = 0; setting <= 3; setting++) {
    EXPECT(PassSetting(false, setting) == setting);
    EXPECT(NoiseSetting(false, setting) == setting);
    EXPECT(TargetSide(false, GamesTargetSide(setting)) == GamesTargetSide(setting));
  }
  // esi untouched, whatever it holds.
  for (uint32_t esi : {0u, 1u, 0x80u, 0x100u, 0x400u, 0xFFFFFFFFu}) EXPECT(TargetSide(false, esi) == esi);
}

// Without the option the game's own sizes, 128, 256, 512 and 1024, are
// of one size among themselves too.
void TestWithoutTheOptionTheGamesOwnSizes() {
  const uint32_t side[] = {128, 256, 512, 1024};
  for (uint32_t setting = 0; setting <= 3; setting++) {
    const Sizes s = Of(false, setting);
    EXPECT(s.target == side[setting]);
    EXPECT(s.noise == side[setting]);
    EXPECT(s.tap == 1.0f / float(side[setting]));
    EXPECT(s.half_texel == 0.5f / float(side[setting]));
  }
}

// The game holds the targets to 1024 past its last setting; the hook
// keeps that without the option and gives 256 with it.
void TestTheGamesLimitOnTheTargets() {
  EXPECT(GamesTargetSide(4) == 0x400);
  EXPECT(TargetSide(false, GamesTargetSide(4)) == 0x400);
  EXPECT(TargetSide(true, GamesTargetSide(4)) == 256);
}

}  // namespace

int main() {
  TestWithTheOptionEveryHookGives256();
  TestWithTheOptionThePassIsTheTargetsSize();
  TestWithoutTheOptionEveryHookGivesTheGamesOwn();
  TestWithoutTheOptionTheGamesOwnSizes();
  TestTheGamesLimitOnTheTargets();
  if (g_failures) return 1;
  std::puts("water_surface_test: ok");
  return 0;
}
