// The game's saves: what the port adds to loading them.
//
//   Always on: the save and load errors the game records through 00857E90
//     are also lines in the log, with the code and, for the errors about
//     one save, its file name. The game itself only shows a message ("Load
//     failed. Please check your hard drive and try again." for most of
//     them) and writes nothing.
#include <cstdint>
#include <string>

#include "guest_string.h"
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

}  // namespace recomp::gen
