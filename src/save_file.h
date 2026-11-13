// What the port's own code knows of the game's save errors, for the hook
// in saves.cpp. Plain C++: tests/save_file_test.cpp
// checks it.
#pragma once

#include <cstdint>

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

}  // namespace save_file
