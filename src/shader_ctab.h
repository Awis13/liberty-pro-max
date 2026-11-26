// The constant table (CTAB comment) of Direct3D 9 shader bytecode: which
// register a named constant or sampler is in. Used by the shader patches
// (sun_shadow_tokens.h, reflection_tokens.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gtaiv_shader {

// D3DXREGISTER_SET values.
inline constexpr uint16_t kBoolSet = 0, kIntSet = 1, kFloatSet = 2, kSamplerSet = 3;

// The register of `name` in register set `set` the CTAB of `tokens` names;
// -1 if none.
inline int CtabRegister(const std::vector<uint32_t>& tokens, const char* name, uint16_t set) {
  for (size_t i = 1; i < tokens.size();) {
    const uint32_t op = tokens[i] & 0xFFFF;
    if (op == 0xFFFF) break;
    if (op != 0xFFFE) {
      i += 1 + ((tokens[i] >> 24) & 0xF);
      continue;
    }
    const size_t n = (tokens[i] >> 16) & 0x7FFF;
    if (i + 1 + n > tokens.size()) break;
    if (n >= 8 && tokens[i + 1] == 0x42415443) {  // 'CTAB'
      const auto* base = reinterpret_cast<const uint8_t*>(&tokens[i + 2]);  // the table's header
      const size_t size = (n - 1) * 4;
      auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, base + at, 4);
        return v;
      };
      auto u16 = [&](size_t at) {
        uint16_t v;
        std::memcpy(&v, base + at, 2);
        return v;
      };
      const uint32_t count = u32(12), info = u32(16);
      for (uint32_t c = 0; c < count; c++) {
        const size_t at = size_t(info) + size_t(c) * 20;
        if (at + 20 > size) break;
        const uint32_t name_at = u32(at);
        if (name_at >= size) continue;
        const char* s = reinterpret_cast<const char*>(base + name_at);
        const size_t len = strnlen(s, size - name_at), want = std::strlen(name);
        if (len == size - name_at || len != want || std::memcmp(s, name, len) != 0) continue;
        if (u16(at + 4) == set) return u16(at + 6);
      }
    }
    i += 1 + n;
  }
  return -1;
}

}  // namespace gtaiv_shader
