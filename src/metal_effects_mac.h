#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace recomp::gfx {
// RAGE's PE32 embedded-resource table stores {name VA, data VA, size, zero}.
// The executable is already part of the compatibility scope. Discover its
// named FXC resources as well as loose files: e.g. the embedded Perlin shader
// differs from the loose version. Never scan arbitrary bytes as shader tokens.
inline std::vector<std::pair<std::string, std::string>> MetalEmbeddedEffects(const std::string& exe) {
  std::vector<std::pair<std::string, std::string>> result;
  auto fits = [&](size_t at, size_t n) { return at <= exe.size() && n <= exe.size() - at; };
  auto word = [&](size_t at) -> uint32_t {
    if (!fits(at, 4)) return 0;
    const auto* b = reinterpret_cast<const unsigned char*>(exe.data() + at);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
  };
  if (!fits(0, 64) || exe.compare(0, 2, "MZ")) return result;
  size_t pe = word(0x3c);
  if (!fits(pe, 24) || exe.compare(pe, 4, "PE\0\0", 4)) return result;
  size_t optional = pe + 24, bytes = word(pe + 20) & 0xffff;
  if (bytes < 32 || !fits(optional, bytes) || (word(optional) & 0xffff) != 0x10b) return result;
  const uint32_t base = word(optional + 28);
  size_t sections = word(pe + 4) >> 16, table = optional + bytes;
  if (!sections || sections > 96 || !fits(table, sections * 40)) return result;
  auto offset = [&](uint32_t va, size_t n) -> size_t {
    if (va < base) return exe.size();
    uint32_t rva = va - base;
    for (size_t i = 0; i < sections; i++) {
      size_t section = table + i * 40;
      uint32_t start = word(section + 12), size = word(section + 16), file = word(section + 20);
      if (rva >= start && uint64_t(rva) - start <= size && n <= size - (rva - start) &&
          fits(size_t(file) + rva - start, n)) return size_t(file) + rva - start;
    }
    return exe.size();
  };
  for (size_t i = 0; i < sections; i++) {
    size_t section = table + i * 40, start = word(section + 20), bytes = word(section + 16);
    if (!fits(start, bytes)) continue;
    for (size_t at = start; at + 16 <= start + bytes; at += 4) {
      uint32_t length = word(at + 8);
      if (length < 8 || length > (1u << 20) || word(at + 12)) continue;
      size_t name = offset(word(at), 8);
      if (name == exe.size() || exe.compare(name, 8, "win32_30")) continue;
      size_t end = exe.find('\0', name);
      if (end == std::string::npos || end - name > 128) continue;
      std::string path = exe.substr(name, end - name);
      if (!path.ends_with(".fxc") || path.find("..") != std::string::npos) continue;
      size_t data = offset(word(at + 4), length);
      if (data == exe.size() || exe.compare(data, 4, "rgxa")) continue;
      result.emplace_back("embedded/" + path, exe.substr(data, length));
    }
  }
  return result;
}
}  // namespace recomp::gfx
