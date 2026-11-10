// Original PE32 fixture exercises embedded discovery without a GPU or game.
#include "metal_effects_mac.h"
#include <cassert>
#include <cstdint>
#include <string>

int main() {
  std::string exe(0x420, '\0');
  auto put = [&](size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) exe[at + i] = char(v >> (8 * i));
  };
  exe.replace(0, 2, "MZ"); put(0x3c, 0x80);
  exe.replace(0x80, 4, "PE\0\0", 4); put(0x84, 0x10000);
  put(0x94, 0xe0); put(0x98, 0x10b); put(0xb4, 0x400000);
  put(0x180, 0x220); put(0x184, 0x1000); put(0x188, 0x220); put(0x18c, 0x200);
  const std::string name = "win32_30/rage_perlinnoise.fxc";
  exe.replace(0x240, name.size(), name);
  put(0x200, 0x401040); put(0x204, 0x401200); put(0x208, 32);
  exe.replace(0x400, 4, "rgxa");
  const std::string valid = exe;
  auto records = recomp::gfx::MetalEmbeddedEffects(exe);
  assert(records.size() == 1 && records[0].first == "embedded/" + name);
  assert(records[0].second == exe.substr(0x400));
  for (size_t n = 0; n < valid.size(); n++)
    assert(recomp::gfx::MetalEmbeddedEffects(valid.substr(0, n)).empty());
  for (auto [at, value] : {std::pair<size_t, uint32_t>{0x3c, 0xffffffff},
                           {0x94, 0xffff}, {0x188, 0xffffffff},
                           {0x200, 0xffffffff}, {0x204, 0x401201},
                           {0x208, 0xffffffff}, {0x20c, 1}}) {
    exe = valid; put(at, value);
    assert(recomp::gfx::MetalEmbeddedEffects(exe).empty());
  }
  exe = valid; exe[0x400] = '?';
  assert(recomp::gfx::MetalEmbeddedEffects(exe).empty());
}
