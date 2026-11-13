// A C string out of the game's memory, read through whatever gives a byte
// of it: hooks pass the runtime's Rd8, tests an array. Plain C++.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace guest_string {

// The string at va: up to its NUL, or `max` characters if none comes
// before. byte(address) reads one byte; nothing past the end is read. va
// is not checked, 0 is read like any other address.
template <class Byte>
std::string Read(uint32_t va, size_t max, Byte&& byte) {
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    const char ch = char(byte(va + uint32_t(i)));
    if (!ch) break;
    s += ch;
  }
  return s;
}

}  // namespace guest_string
