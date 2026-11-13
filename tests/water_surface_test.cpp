// water_surface (src/water_surface.h): what the water surface's hooks
// give the game. With the tiling option the surface's render targets,
// the pass that draws one from the other and the noise texture are all
// 256x256 at every setting; without it each hook gives back the game's
// own value. And the surface's two render targets, and no others, are
// created inside a scope, which is over when the creation is: the port's
// whole function for the game's creation, on a render target object in
// memory of the test's own, with a scope that records where it began and
// ended in place of the runtime's.

#include "water_surface.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

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

using water_surface::CreateTarget;
using water_surface::CreateTargetOf;
using water_surface::SimulationTarget;
using water_surface::TargetName;

const char* const kSimulation[] = {"WATER_SURFACE0_COLOUR", "WATER_SURFACE1_COLOUR"};
// The other render targets of the function that sets the surface's up
// (00A0D6D0), a scene buffer's name, and near misses.
const char* const kOthers[] = {"WATER_REFLECTION_COLOUR", "WATER_REFLECTION_DEPTH", "REFLECTION_MAP_COLOUR",
                               "REFLECTION_MAP_DEPTH",    "BackBuffer",             "",
                               "WATER_SURFACE",           "WATER_SURFACE0_COLOUR2", "WATER_SURFACE2_COLOUR",
                               "WATER_SURFACE0_DEPTH",    "water_surface0_colour",  " WATER_SURFACE0_COLOUR"};

void TestWhichTargetsAreTheSimulations() {
  for (const char* name : kSimulation) EXPECT(SimulationTarget(name));
  for (const char* name : kOthers) EXPECT(!SimulationTarget(name));
}

// In place of the runtime's scope: what happened, in order. '(' a scope
// began, ')' it ended, and the tests' creations add 'c'.
std::string g_log;
struct Scope {
  Scope() { g_log += '('; }
  ~Scope() { g_log += ')'; }
};
// What happened during f().
template <class F>
std::string Log(F&& f) {
  g_log.clear();
  f();
  return g_log;
}
void Create() { g_log += 'c'; }

void TestTheSimulationsTargetsAreCreatedInTheScope() {
  for (const char* name : kSimulation) EXPECT(Log([&] { CreateTarget<Scope>(name, Create); }) == "(c)");
}

void TestTheOtherTargetsAreCreatedOutsideIt() {
  for (const char* name : kOthers) EXPECT(Log([&] { CreateTarget<Scope>(name, Create); }) == "c");
}

// Creations one after the other: each as if alone, no scope left over
// from the one before.
void TestARunOfTargets() {
  const std::pair<const char*, const char*> run[] = {
      {"WATER_REFLECTION_COLOUR", "c"}, {"WATER_SURFACE0_COLOUR", "(c)"}, {"WATER_SURFACE1_COLOUR", "(c)"},
      {"REFLECTION_MAP_COLOUR", "c"},   {"WATER_SURFACE0_COLOUR", "(c)"}, {"REFLECTION_MAP_DEPTH", "c"}};
  std::string all, want;
  for (size_t i = 0; i < std::size(run); i++) {
    all += Log([&] { CreateTarget<Scope>(run[i].first, Create); });
    want += run[i].second;
  }
  EXPECT(all == want);
}

// A creation that does not come back the usual way leaves no scope open.
void TestTheScopeEndsWithACreationThatThrows() {
  const std::string log = Log([] {
    try {
      CreateTarget<Scope>(kSimulation[0], [] {
        Create();
        throw std::runtime_error("creation");
      });
      EXPECT(!"thrown past CreateTarget");
    } catch (const std::runtime_error&) {
      g_log += '!';
    }
  });
  EXPECT(log == "(c)!");
}

// A render target object as the game's creation finds it, in memory of
// the test's own at kObject: a pointer to its name at +30 and, right
// before it at +2C, one to another name, which nothing may take for it.
constexpr uint32_t kObject = 0x2000, kName = 0x2100, kDecoy = 0x2200, kMemory = 0x400;
struct Target {
  uint8_t mem[kMemory] = {};
  int reads = 0;

  // name: at +30 (null: no pointer there); decoy: at +2C.
  Target(const char* name, const char* decoy) {
    Put(0x2C, kDecoy, decoy);
    if (name) Put(0x30, kName, name);
  }
  void Put(uint32_t field, uint32_t va, const char* text) {
    std::memcpy(mem + field, &va, 4);  // (the test's host is little-endian, as the game's is)
    std::strcpy(reinterpret_cast<char*>(mem) + (va - kObject), text);
  }
  uint8_t Byte(uint32_t va) {
    reads++;
    EXPECT(va >= kObject && va - kObject < kMemory);
    return va >= kObject && va - kObject < kMemory ? mem[va - kObject] : 0;
  }
  uint32_t Dword(uint32_t va) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = v << 8 | Byte(va + uint32_t(i));
    return v;
  }
  // The port's 00423BA0 on this object: what happened.
  std::string Created() {
    return Log([this] {
      CreateTargetOf<Scope>(
          kObject, [this](uint32_t va) { return Byte(va); }, [this](uint32_t va) { return Dword(va); }, Create);
    });
  }
};

// The game's creation runs exactly once each time: in the scope for the
// simulation's two, outside it for another name and for none.
void TestTheGamesCreationOnATargetObject() {
  for (const char* name : kSimulation) EXPECT(Target(name, "REFLECTION_MAP_COLOUR").Created() == "(c)");
  for (const char* name : kOthers) EXPECT(Target(name, kSimulation[0]).Created() == "c");
  // The name is the one at +30, whatever the pointer before it leads to.
  EXPECT(Target(kSimulation[0], kSimulation[1]).Created() == "(c)");
  EXPECT(Target("REFLECTION_MAP_DEPTH", kSimulation[1]).Created() == "c");
  // No name: created all the same, and only the pointer was read.
  Target nameless(nullptr, kSimulation[0]);
  EXPECT(nameless.Created() == "c");
  EXPECT(nameless.reads == 4);
}

// The name as the hook reads it: bytes of the game's memory, here an
// array standing at an address.
void TestTheNameOutOfTheGamesMemory() {
  const uint32_t at = 0x1000;
  char mem[128];
  std::memset(mem, 'x', sizeof mem);
  auto byte = [&](uint32_t va) { return uint8_t(mem[va - at]); };
  std::memcpy(mem + 8, "WATER_SURFACE1_COLOUR", 22);  // with its NUL; 'x' goes on after it
  EXPECT(TargetName(at + 8, byte) == "WATER_SURFACE1_COLOUR");
  EXPECT(SimulationTarget(TargetName(at + 8, byte)));
  EXPECT(TargetName(at + 9, byte) == "ATER_SURFACE1_COLOUR");
  mem[40] = 0;
  EXPECT(TargetName(at + 40, byte).empty());
  // No name at all: nothing is read.
  int reads = 0;
  EXPECT(TargetName(0, [&](uint32_t) { return reads++, uint8_t('W'); }).empty());
  EXPECT(reads == 0);
  // No end in sight: cut, and not read past the cut.
  uint32_t last = 0;
  const std::string cut = TargetName(at, [&](uint32_t va) { return last = va, uint8_t('W'); });
  EXPECT(cut.size() == water_surface::kLongestName);
  EXPECT(last == at + water_surface::kLongestName - 1);
  EXPECT(!SimulationTarget(cut));
}

}  // namespace

int main() {
  TestWithTheOptionEveryHookGives256();
  TestWithTheOptionThePassIsTheTargetsSize();
  TestWithoutTheOptionEveryHookGivesTheGamesOwn();
  TestWithoutTheOptionTheGamesOwnSizes();
  TestTheGamesLimitOnTheTargets();
  TestWhichTargetsAreTheSimulations();
  TestTheSimulationsTargetsAreCreatedInTheScope();
  TestTheOtherTargetsAreCreatedOutsideIt();
  TestARunOfTargets();
  TestTheScopeEndsWithACreationThatThrows();
  TestTheGamesCreationOnATargetObject();
  TestTheNameOutOfTheGamesMemory();
  if (g_failures) return 1;
  std::puts("water_surface_test: ok");
  return 0;
}
