// The game's saves: what the port adds to loading them.
//
//   Always on: the save and load errors the game records through 00857E90
//     are also lines in the log, with the code and, for the errors about
//     one save, its file name. The game itself only shows a message ("Load
//     failed. Please check your hard drive and try again." for most of
//     them) and writes nothing.
//   GTAIV_SAVE_LIVE_CHECKSUM (on by default): a save made with the real
//     Games for Windows Live can be loaded. Its checksum is sealed by Live
//     and cannot be read here, so the game's comparison of the checksum
//     with the data is passed when the save's own sum of its data is
//     right. Off: the game's own behaviour, such a save is refused.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "guest_string.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"
#include "save_file.h"

namespace recomp::gen {

void f_00857E90_lifted(Ctx& c);

namespace {

// The file name of the save in use, "SGTA4" and the slot's two digits
// (00857C70 writes it for the slot being read or written).
constexpr gaddr kSaveFileName = 0x011D0380;
constexpr size_t kSaveFileNameMax = 15;  // twice what "SGTA4" and two digits take

std::string SaveFileName() {
  return guest_string::Read(kSaveFileName, kSaveFileNameMax, [](uint32_t va) { return Rd8(va); });
}

// The checksum of a save's data (a CRC-32): ecx the bytes, cdecl (the
// value to start from, their count).
constexpr gaddr kChecksum = 0x00466AE0;
// The sizes of this game's 32 blocks, worked out before a load's read
// (0085B4A0 fills the load's record at 011D02E0; the sizes are at +10h,
// where 0085B080 compares each block's with them).
constexpr gaddr kLoadBlockSizes = 0x011D02F0;
// A longer trailer than any save's is not looked at: the file is not one.
constexpr uint32_t kTrailerMax = 1u << 20;

}  // namespace

// cdecl (code): where the game records a save or load error, for the
// message 008597C0 then shows. The questions and device states it also
// records are no errors and are not logged (save_file::ErrorText).
void f_00857E90(Ctx& c) {
  const uint32_t code = Rd32(c.esp + 4);
  if (const char* text = save_file::ErrorText(code)) {
    if (save_file::ErrorHasFile(code))
      WarnF("gtaiv: save: the game's error %02X (%s), file %s\n", code, text, SaveFileName().c_str());
    else
      WarnF("gtaiv: save: the game's error %02X (%s)\n", code, text);
  }
  f_00857E90_lifted(c);
}

// The load's comparison of a save's checksum (0061EC60). Replaced: call
// 00466AE0, the checksum of the data after its first 10h bytes, which the
// game then compares with the first 4 bytes of the unsealed trailer. Here
// ecx is those bytes of the data, ebx their count, esi the trailer's size
// in the file and edi the unsealed trailer, which the runtime's Live gives
// back as it is in the file. What the file is and what the game is then
// given to compare: save_file.h (Classify, Decide).
void hook_0061ED8D(Ctx& c) {
  static const bool sealed_loads = OptionOn("GTAIV_SAVE_LIVE_CHECKSUM", true);
  const uint32_t data = c.ecx - save_file::kHeaderSize, data_size = c.ebx + save_file::kHeaderSize;
  const uint32_t trailer = c.edi, trailer_size = c.esi;
  const uint32_t checksum = uint32_t(CallGuest(c, kChecksum, {0, c.ebx}));
  c.eax = checksum;
  if (trailer_size > kTrailerMax) return;
  // The file again, from the two pieces the game has made of it.
  std::vector<uint8_t> file(size_t(data_size) + trailer_size);
  std::memcpy(file.data(), GPtr(data), data_size);
  if (trailer_size) std::memcpy(file.data() + data_size, GPtr(trailer), trailer_size);
  uint32_t game_blocks[save_file::kBlocks];
  for (uint32_t i = 0; i < save_file::kBlocks; i++) game_blocks[i] = Rd32(kLoadBlockSizes + 4 * i);
  const save_file::Save save = save_file::Classify(file.data(), uint32_t(file.size()), game_blocks);
  const save_file::Decision d = save_file::Decide(sealed_loads, save.kind, save.sum_ok, checksum, Rd32(trailer));
  c.eax = d.checksum;
  const std::string name = SaveFileName();
  const char* const f = name.c_str();
  switch (d.line) {
    case save_file::Line::kNone:
      break;
    case save_file::Line::kSealedLoaded:
      WarnF("gtaiv: save: %s: sealed by Games for Windows Live, its checksum cannot be read and its own sum is "
            "right: loaded\n", f);
      break;
    case save_file::Line::kSealedSwitchOff:
      WarnF("gtaiv: save: %s: sealed by Games for Windows Live, its checksum cannot be read "
            "(GTAIV_SAVE_LIVE_CHECKSUM is off): not loaded\n", f);
      break;
    case save_file::Line::kSealedDamaged:
      WarnF("gtaiv: save: %s: damaged (sealed by Games for Windows Live, and its own sum is not its data's): not "
            "loaded\n", f);
      break;
    case save_file::Line::kChecksumWrong:
      WarnF("gtaiv: save: %s: damaged or edited (its checksum is not its data's): not loaded\n", f);
      break;
    case save_file::Line::kOtherVersion:
      WarnF("gtaiv: save: %s: made by another version of the game (block %u is %u bytes, %u in 1.0.8.0), as the "
            "Complete Edition's saves are: not loaded\n", f, save.block, save.block_size, save.game_block_size);
      break;
    case save_file::Line::kDamaged:
      WarnF("gtaiv: save: %s: damaged (not laid out as a save, or its end is not a checksum's): not loaded\n", f);
      break;
  }
}

}  // namespace recomp::gen
