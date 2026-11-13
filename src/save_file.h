// What the port's own code knows of a save file and of the game's save
// errors, for the hooks in saves.cpp. Plain C++: tests/save_file_test.cpp
// checks it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace save_file {

// The game's save and load errors that go through 00857E90: the code it
// stores for its message (in [00F348D8]) and the text key the message is
// shown from (the switch in 008597C0). Null for the codes that are no
// error: none, the questions (overwrite, autosave, sign in) and the states
// of the storage device.
constexpr const char* ErrorText(uint32_t code) {
  switch (code) {
    case 0x07: case 0x08: case 0x09: return "SG_DEV_SEL_FAIL";
    case 0x0A: case 0x0B: case 0x0C: return "SG_SPCHK_FAILED";
    case 0x0D: return "SG_DEL_FAILED";
    case 0x0E: case 0x0F: case 0x10: return "SG_ENUM_FAILED";
    case 0x11: return "SG_BGNSAVE_FAIL";
    case 0x12: case 0x13: return "SG_CHKSAVE_FAIL";
    case 0x14: return "SG_BGNICON_FAIL";
    case 0x15: case 0x16: return "SG_CHKICON_FAIL";
    case 0x17: return "PC_BGNCRTR_FAIL";
    case 0x18: return "PC_CHKCRTR_FAIL";
    case 0x19: return "SG_CHKCRTR_FAIL";
    case 0x1A: return "SG_NOT_CREATOR";
    case 0x1B: return "PC_BGNLOAD_FAIL";  // the file was not read, or its checksum is not its data's
    case 0x1C: return "SG_BUFFSIZE_DIF";
    case 0x1D: case 0x1E: return "SG_CHKLOAD_FAIL";  // the data is not laid out as this game's
    case 0x1F: return "SG_ALLOC_MEM";
    case 0x20: return "SG_SAVE_BLOCK";
    case 0x2A: return "SG_TOOMANYFILES";
    case 0x2B: return "SG_FILENAME_ERR";
    case 0x2C: return "SG_FILENAME_DUP";
    case 0x2D: return "SG_VERS_ERR";
    case 0x2E: return "FE_NODLC2";  // the save needs downloadable content that is not there (FE_NODLC3 in an autoload)
    default: return nullptr;
  }
}

// Whether an error is about the one save file being written or read (the
// name 00857C70 last made is that file's), not about the device, the list
// of saves or their names.
constexpr bool ErrorHasFile(uint32_t code) { return (code >= 0x11 && code <= 0x20) || code == 0x2E; }

// A save file, as the game writes it (0085AE30) and reads it (0085B080):
//   +000  version, the file's size, the game it is of, "SAVE" (4 bytes each)
//   +010  the name the menu shows, 100h bytes of UTF-16
//   +110  32 blocks: "BLOCK", the block's size (4 bytes, these 9 counted),
//         its body
//         the sum of every byte before it (4 bytes), "END" and a zero
// That is the data. After it comes a trailer with the data's checksum
// (0061EB10): Games for Windows Live seals 12 bytes for the game, the
// checksum (a CRC-32 of the data after its first 10h bytes) and 8 unused
// ones.
constexpr uint32_t kHeaderSize = 0x10;
constexpr uint32_t kSizeAt = 4;  // the file's size, in the header
constexpr uint32_t kNameSize = 0x100;
constexpr uint32_t kBlocks = 32;
constexpr uint32_t kBlockHeadSize = 9;
constexpr uint32_t kEndSize = 8;  // the sum, "END" and its zero
// The trailer where sealing is a plain copy (this port, and the Live
// replacements on PC): the 12 bytes themselves.
constexpr uint32_t kPlainTrailerSize = 12;
// The trailer the real Live makes of those 12 bytes, as far as it is the
// same in every save seen: 360 bytes; the size of its head, 128h, in its
// first 4 bytes; 8 fixed bytes after them; at 124h the size of what was
// sealed, 12; then 40h bytes only Live can open.
constexpr uint32_t kSealedTrailerSize = 360;
constexpr uint32_t kSealedHeadSize = 0x128;
constexpr uint8_t kSealedMark[8] = {0x02, 0x03, 0x01, 0x00, 0x03, 0x03, 0x00, 0x00};
constexpr uint32_t kSealedMarkAt = 4;
constexpr uint32_t kSealedPlainSizeAt = 0x124;

// 4 bytes of a file, low byte first.
inline uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// The blocks of the save in p[0..n): each one's size, and where the data
// ends, counted from p: just after "END" and its zero. False when the
// bytes are not laid out as a save's within n. Nothing at or past p + n is
// read.
struct Blocks {
  uint32_t size[kBlocks];
  uint32_t end;
};
inline bool Walk(const uint8_t* p, uint32_t n, Blocks& blocks) {
  uint32_t at = kHeaderSize + kNameSize;
  if (n < at || std::memcmp(p + 0xC, "SAVE", 4) != 0) return false;
  for (uint32_t i = 0; i < kBlocks; i++) {
    if (n - at < kBlockHeadSize || std::memcmp(p + at, "BLOCK", 5) != 0) return false;
    const uint32_t size = Le32(p + at + 5);
    if (size > n - at) return false;
    blocks.size[i] = size;
    at += size;
  }
  if (n - at < kEndSize || std::memcmp(p + at + 4, "END", 4) != 0) return false;
  blocks.end = at + kEndSize;
  return true;
}

// Where the data of the save in p[0..n) ends, 0 when it is not a save's.
inline uint32_t DataEnd(const uint8_t* p, uint32_t n) {
  Blocks blocks;
  return Walk(p, n, blocks) ? blocks.end : 0;
}

// The sum the game writes before "END" (00857510): of every byte of the
// data before the sum itself. The header's size is then still the data's
// own (0085AE30); the trailer's is added to it after (0061EB10). The game
// reads the sum back and does nothing with it (0085B080). end: where the
// data ends (Walk).
inline uint32_t BodySum(const uint8_t* p, uint32_t end) {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < end - kEndSize; i++)
    sum += i >= kSizeAt && i < kSizeAt + 4 ? uint8_t(end >> (8 * (i - kSizeAt))) : p[i];
  return sum;
}
inline bool BodySumMatches(const uint8_t* p, uint32_t end) { return Le32(p + end - kEndSize) == BodySum(p, end); }

// Whether a trailer is one the real Live made (the constants above).
inline bool IsSealedTrailer(const uint8_t* t, uint32_t size) {
  return size == kSealedTrailerSize && Le32(t) == kSealedHeadSize &&
         std::memcmp(t + kSealedMarkAt, kSealedMark, sizeof kSealedMark) == 0 &&
         Le32(t + kSealedPlainSizeAt) == kPlainTrailerSize;
}

// What a save file is to the load. The game reads the whole file, takes
// as data the size its own saves' data has now (0085B4A0 works out each
// block's size before the read) and as trailer the rest, unseals the
// trailer and refuses the save unless its first 4 bytes are the data's
// checksum (0061EC60).
enum class Kind {
  // This game's blocks and the 12 plain bytes: the game's comparison tells
  // a sound save from a damaged one.
  kPlain,
  // This game's blocks and Live's sealed trailer: the checksum cannot be
  // had. What is left to check is the data's own sum (Save::sum_ok).
  kSealed,
  // A save, but one of its blocks is not the size this game's is: made by
  // another version of the game (the Complete Edition's saves have larger
  // blocks). The game cannot read it.
  kOtherVersion,
  // Anything else: not laid out as a save, or with a trailer that is
  // neither of the two.
  kDamaged,
};
struct Save {
  Kind kind;
  bool sum_ok = false;  // kSealed: the data's sum is its bytes'
  // kOtherVersion: the first block of another size (0 the first), its size
  // in the file and in this game.
  uint32_t block = 0, block_size = 0, game_block_size = 0;
};

// file: the whole file, n bytes. game_blocks: the kBlocks sizes this
// game's blocks have.
inline Save Classify(const uint8_t* file, uint32_t n, const uint32_t* game_blocks) {
  Blocks blocks;
  if (!Walk(file, n, blocks)) return {Kind::kDamaged};
  for (uint32_t i = 0; i < kBlocks; i++)
    if (blocks.size[i] != game_blocks[i]) return {Kind::kOtherVersion, false, i, blocks.size[i], game_blocks[i]};
  const uint32_t trailer_size = n - blocks.end;
  if (trailer_size == kPlainTrailerSize) return {Kind::kPlain};
  if (IsSealedTrailer(file + blocks.end, trailer_size)) return {Kind::kSealed, BodySumMatches(file, blocks.end)};
  return {Kind::kDamaged};
}

// What the load's comparison is given, and what the log is told.
enum class Line {
  kNone,
  kSealedLoaded,     // sealed, its sum right: loaded without the comparison
  kSealedSwitchOff,  // sealed, and the switch is off
  kSealedDamaged,    // sealed, its sum wrong
  kChecksumWrong,    // plain, the checksum is not the data's
  kOtherVersion,
  kDamaged,
};
struct Decision {
  uint32_t checksum;  // what the game compares with the trailer's first 4 bytes
  Line line;
};

// sealed_loads: the switch. checksum: the data's, as the game worked it
// out. trailer_word: the trailer's first 4 bytes. The game is given its
// own checksum in every case but one, a sealed save with the right sum
// and the switch on: there the trailer's own bytes, so that the save
// loads. In every other case the game's comparison decides as it always
// did; the log is told why when it refuses.
constexpr Decision Decide(bool sealed_loads, Kind kind, bool sum_ok, uint32_t checksum, uint32_t trailer_word) {
  if (sealed_loads && kind == Kind::kSealed && sum_ok) return {trailer_word, Line::kSealedLoaded};
  if (checksum == trailer_word) return {checksum, Line::kNone};
  switch (kind) {
    case Kind::kPlain: return {checksum, Line::kChecksumWrong};
    case Kind::kSealed:
      if (!sealed_loads) return {checksum, Line::kSealedSwitchOff};
      return {checksum, Line::kSealedDamaged};
    case Kind::kOtherVersion: return {checksum, Line::kOtherVersion};
    case Kind::kDamaged: break;
  }
  return {checksum, Line::kDamaged};
}

}  // namespace save_file
