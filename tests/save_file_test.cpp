// save_file (src/save_file.h): which of the game's save codes are errors.

#include "save_file.h"

#include <cstdio>
#include <string_view>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "save_file_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

// ---- the game's error codes -------------------------------------------------

// The game's own table: the key its message is shown from, for each code
// its switch (008597C0) takes straight to a failure message.
struct Key {
  uint32_t code;
  const char* key;
};
constexpr Key kGameKeys[] = {
    {0x07, "SG_DEV_SEL_FAIL"}, {0x08, "SG_DEV_SEL_FAIL"}, {0x09, "SG_DEV_SEL_FAIL"}, {0x0A, "SG_SPCHK_FAILED"},
    {0x0B, "SG_SPCHK_FAILED"}, {0x0C, "SG_SPCHK_FAILED"}, {0x0D, "SG_DEL_FAILED"},   {0x0E, "SG_ENUM_FAILED"},
    {0x0F, "SG_ENUM_FAILED"},  {0x10, "SG_ENUM_FAILED"},  {0x11, "SG_BGNSAVE_FAIL"}, {0x12, "SG_CHKSAVE_FAIL"},
    {0x13, "SG_CHKSAVE_FAIL"}, {0x14, "SG_BGNICON_FAIL"}, {0x15, "SG_CHKICON_FAIL"}, {0x16, "SG_CHKICON_FAIL"},
    {0x17, "PC_BGNCRTR_FAIL"}, {0x18, "PC_CHKCRTR_FAIL"}, {0x19, "SG_CHKCRTR_FAIL"}, {0x1A, "SG_NOT_CREATOR"},
    {0x1B, "PC_BGNLOAD_FAIL"}, {0x1C, "SG_BUFFSIZE_DIF"}, {0x1D, "SG_CHKLOAD_FAIL"}, {0x1E, "SG_CHKLOAD_FAIL"},
    {0x1F, "SG_ALLOC_MEM"},    {0x20, "SG_SAVE_BLOCK"},   {0x2A, "SG_TOOMANYFILES"}, {0x2B, "SG_FILENAME_ERR"},
    {0x2C, "SG_FILENAME_DUP"}, {0x2D, "SG_VERS_ERR"},     {0x2E, "FE_NODLC2"},
};

// Every code up to past the game's last one: the game's key for the
// failures, nothing for the rest (none, sign in, the device gone, the
// questions before a save overwrites or an autoload, the autosave's
// notices 24 to 26, the episodes' notice, and 32, which the game does not
// record through 00857E90).
void TestErrorText() {
  for (uint32_t code = 0; code <= 0x40; code++) {
    const char* want = nullptr;
    for (const Key& k : kGameKeys)
      if (k.code == code) want = k.key;
    const char* got = save_file::ErrorText(code);
    const bool same = want ? got && std::string_view(got) == want : got == nullptr;
    if (!same)
      std::fprintf(stderr, "code %02X: %s, the game's is %s\n", code, got ? got : "none", want ? want : "none");
    EXPECT(same);
  }
  EXPECT(save_file::ErrorText(0x100) == nullptr);
}

// The file is named for the errors of a save being written or read, not
// for the device's, the list's and the names'.
void TestErrorHasFile() {
  using save_file::ErrorHasFile;
  for (uint32_t code = 0x11; code <= 0x20; code++) EXPECT(ErrorHasFile(code));
  EXPECT(ErrorHasFile(0x2E));
  for (uint32_t code = 0x00; code <= 0x10; code++) EXPECT(!ErrorHasFile(code));
  for (uint32_t code = 0x21; code <= 0x2D; code++) EXPECT(!ErrorHasFile(code));
  for (uint32_t code = 0x2F; code <= 0x40; code++) EXPECT(!ErrorHasFile(code));
}

}  // namespace

int main() {
  TestErrorText();
  TestErrorHasFile();
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("save_file_test: ok");
  return 0;
}
