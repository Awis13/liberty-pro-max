// save_file (src/save_file.h): which of the game's save codes are errors,
// how a save is laid out, what its trailer is and what the load's
// comparison is given. The saves here are made up: the layout only, with
// small blocks; of real files only the fixed fields of Live's trailer.
// Built with the address sanitizer where there is one: a read at or past
// the end of a buffer fails the test.

#include "save_file.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

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

// ---- buffers ----------------------------------------------------------------

// The first n bytes of p in a block of memory of just n bytes.
std::unique_ptr<uint8_t[]> Exact(const uint8_t* p, size_t n) {
  std::unique_ptr<uint8_t[]> m(new uint8_t[n]);
  if (n) std::memcpy(m.get(), p, n);
  return m;
}
uint32_t EndOfFirst(const std::vector<uint8_t>& d, size_t n) {
  return save_file::DataEnd(Exact(d.data(), n).get(), uint32_t(n));
}
uint32_t End(const std::vector<uint8_t>& d) { return EndOfFirst(d, d.size()); }

void Put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) d[at + i] = uint8_t(v >> (8 * i));
}

// ---- a save written out by hand ----------------------------------------------

// The smallest save there is, every block empty, each piece put at its
// offset by hand: 110h of header and name, 32 blocks of 9 bytes
// (110h + 32 * 9 = 230h), the sum at 230h, END at 234h, 238h bytes in all.
constexpr size_t kSmallestSize = 0x238;
constexpr size_t kSmallestSumAt = 0x230;
// Its bytes added up: 39h (the version) + 38h + 02h (the size, 238h) +
// 'S' 'A' 'V' 'E' (303) + 32 * ('B' 'L' 'O' 'C' 'K' (363) + 9).
constexpr uint32_t kSmallestSum = 0x39 + 0x38 + 0x02 + 303 + 32 * (363 + 9);

std::vector<uint8_t> Smallest() {
  std::vector<uint8_t> d(kSmallestSize, 0);
  d[0x000] = 0x39;
  d[0x004] = 0x38;
  d[0x005] = 0x02;
  d[0x00C] = 'S', d[0x00D] = 'A', d[0x00E] = 'V', d[0x00F] = 'E';
  for (size_t at = 0x110; at < 0x230; at += 9) {
    d[at] = 'B', d[at + 1] = 'L', d[at + 2] = 'O', d[at + 3] = 'C', d[at + 4] = 'K';
    d[at + 5] = 9;
  }
  d[kSmallestSumAt] = uint8_t(kSmallestSum), d[kSmallestSumAt + 1] = uint8_t(kSmallestSum >> 8);
  d[0x234] = 'E', d[0x235] = 'N', d[0x236] = 'D';
  return d;
}

void TestSmallestSave() {
  const std::vector<uint8_t> d = Smallest();
  EXPECT(d.size() == kSmallestSize);
  EXPECT(End(d) == kSmallestSize);
  save_file::Blocks blocks;
  EXPECT(save_file::Walk(d.data(), uint32_t(d.size()), blocks));
  for (uint32_t size : blocks.size) EXPECT(size == 9);
  EXPECT(save_file::BodySum(d.data(), kSmallestSize) == kSmallestSum);
  EXPECT(save_file::BodySumMatches(d.data(), kSmallestSize));
  // Each byte of each word it is known by: another letter, and it is no save.
  for (size_t at : {0x00C, 0x00D, 0x00E, 0x00F}) {  // SAVE
    std::vector<uint8_t> bad = d;
    bad[at] ^= 0x20;
    EXPECT(End(bad) == 0);
  }
  for (size_t block : {0x110, 0x119, 0x227}) {  // the first, the second and the last BLOCK
    for (size_t i = 0; i < 5; i++) {
      std::vector<uint8_t> bad = d;
      bad[block + i] ^= 0x20;
      EXPECT(End(bad) == 0);
    }
  }
  for (size_t at : {0x234, 0x235, 0x236, 0x237}) {  // END and its zero
    std::vector<uint8_t> bad = d;
    bad[at] ^= 0x20;
    EXPECT(End(bad) == 0);
  }
  // Cut short by any number of bytes, in memory of just that size: no
  // save, and nothing read past it.
  for (size_t n = 0; n < kSmallestSize; n++) EXPECT(EndOfFirst(d, n) == 0);
  // 31 blocks and 33: END is not where the 33rd block or the 32nd's end is.
  std::vector<uint8_t> fewer = d;
  fewer.erase(fewer.begin() + 0x227, fewer.begin() + 0x230);
  EXPECT(End(fewer) == 0);
  std::vector<uint8_t> more = d;
  more.insert(more.begin() + 0x230, d.begin() + 0x227, d.begin() + 0x230);
  EXPECT(End(more) == 0);
}

// ---- saves with blocks of any size ---------------------------------------------

// A save's data with blocks of these sizes (9 and up each), its sum right.
std::vector<uint8_t> Data(const std::vector<uint32_t>& blocks) {
  std::vector<uint8_t> d(0x110, 0);
  d[0] = 0x39;
  std::memcpy(&d[0xC], "SAVE", 4);
  for (uint32_t size : blocks) {
    const size_t at = d.size();
    d.resize(at + size, 0xAB);
    std::memcpy(&d[at], "BLOCK", 5);
    Put32(d, at + 5, size);
  }
  const size_t at = d.size();
  d.resize(at + 8, 0);
  std::memcpy(&d[at + 4], "END", 4);
  Put32(d, 4, uint32_t(d.size()));
  uint32_t sum = 0;
  for (size_t i = 0; i < at; i++) sum += d[i];
  Put32(d, at, sum);
  return d;
}

// The sizes of a made-up game's 32 blocks: 19 with something in them (20
// bytes and up), the rest empty, as in the game's.
constexpr uint32_t kTenthBlock = 40;        // the size of block 9, the tenth
constexpr uint32_t kTenthBlockOther = 4540;  // and in a save of another version
std::vector<uint32_t> GameBlocks(uint32_t tenth = kTenthBlock) {
  std::vector<uint32_t> b(32, 9);
  for (size_t i = 0; i < 19; i++) b[i] = 20 + uint32_t(i);
  b[9] = tenth;
  return b;
}
// Where the tenth block starts in such a save.
constexpr size_t kTenthBlockAt = 0x110 + 20 + 21 + 22 + 23 + 24 + 25 + 26 + 27 + 28;

// The file: the data, the trailer, and the header's size the whole file's.
std::vector<uint8_t> File(std::vector<uint8_t> data, const std::vector<uint8_t>& trailer) {
  data.insert(data.end(), trailer.begin(), trailer.end());
  Put32(data, 4, uint32_t(data.size()));
  return data;
}

// The 12 plain bytes: a checksum and 8 more.
std::vector<uint8_t> PlainTrailer() { return {0x11, 0x22, 0x33, 0x44, 0xCD, 0xCD, 0xCD, 0xCD, 1, 2, 3, 4}; }

// Live's trailer: 360 bytes with its fixed fields, the rest whatever.
constexpr size_t kSealedSize = 360;
std::vector<uint8_t> SealedTrailer(size_t size = kSealedSize) {
  std::vector<uint8_t> t(size, 0x5A);
  if (size >= 12) {
    Put32(t, 0, 0x128);
    const uint8_t mark[8] = {2, 3, 1, 0, 3, 3, 0, 0};
    std::memcpy(&t[4], mark, 8);
  }
  if (size >= 0x128) Put32(t, 0x124, 12);
  return t;
}

void TestWalk() {
  const std::vector<uint8_t> d = Data(GameBlocks());
  EXPECT(End(d) == d.size());
  save_file::Blocks blocks;
  EXPECT(save_file::Walk(d.data(), uint32_t(d.size()), blocks));
  const std::vector<uint32_t> want = GameBlocks();
  for (size_t i = 0; i < 32; i++) EXPECT(blocks.size[i] == want[i]);
  EXPECT(blocks.end == d.size());
  // With a trailer after it the data ends where it did.
  EXPECT(End(File(d, PlainTrailer())) == d.size());
  EXPECT(End(File(d, SealedTrailer())) == d.size());
  // Cut inside the tenth block, at its start, and with 1 to 8 bytes of a
  // block's first 9 left: no save.
  EXPECT(EndOfFirst(d, kTenthBlockAt + 20) == 0);
  for (size_t left = 0; left < 9; left++) EXPECT(EndOfFirst(d, kTenthBlockAt + left) == 0);
  // Cut 1 to 8 bytes before the end, and in the last blocks.
  for (size_t cut = 1; cut <= 8 + 2 * 9; cut++) EXPECT(EndOfFirst(d, d.size() - cut) == 0);
  // The rest of the save still in memory after n: not looked at. A size
  // that goes past n by 1 to 8 bytes, a block cut 5 bytes in, and n inside
  // the tenth block.
  const size_t last = d.size() - 8 - 9;  // the last block
  for (size_t over = 1; over <= 8; over++) EXPECT(save_file::DataEnd(d.data(), uint32_t(last + 9 - over)) == 0);
  EXPECT(save_file::DataEnd(d.data(), uint32_t(last - 9 + 5)) == 0);
  EXPECT(save_file::DataEnd(d.data(), uint32_t(kTenthBlockAt + 20)) == 0);
  // A block's size that cannot be: past the end by one, far past it, none
  // (the walk stays on that block and finds no END after 32 of it).
  std::vector<uint8_t> bad = d;
  Put32(bad, 0x110 + 5, uint32_t(d.size() - 0x110 + 1));
  EXPECT(End(bad) == 0);
  Put32(bad, 0x110 + 5, 0xFFFFFFF0u);
  EXPECT(End(bad) == 0);
  Put32(bad, 0x110 + 5, 0);
  EXPECT(End(bad) == 0);
  // A size one more or less than the block's: the next BLOCK is not there.
  bad = d;
  Put32(bad, kTenthBlockAt + 5, kTenthBlock + 1);
  EXPECT(End(bad) == 0);
  Put32(bad, kTenthBlockAt + 5, kTenthBlock - 1);
  EXPECT(End(bad) == 0);
  // END with something else than its zero after it, BLOCK with another
  // fifth letter, SAVE with another last one.
  bad = d;
  bad[bad.size() - 1] = 'S';
  EXPECT(End(bad) == 0);
  bad = d;
  bad[kTenthBlockAt + 4] = 'S';
  EXPECT(End(bad) == 0);
  bad = d;
  bad[0xF] = 'D';
  EXPECT(End(bad) == 0);
}

// The sum before END: of the bytes before it, with the header's size the
// data's own whatever the file's is.
void TestBodySum() {
  using save_file::BodySumMatches;
  const std::vector<uint8_t> d = Data(GameBlocks());
  const uint32_t end = uint32_t(d.size());
  EXPECT(BodySumMatches(d.data(), end));
  const std::vector<uint8_t> sealed = File(d, SealedTrailer());
  EXPECT(save_file::Le32(sealed.data() + 4) == end + kSealedSize);
  EXPECT(BodySumMatches(sealed.data(), end));
  // One byte other in a block's body, in the name, in the first byte.
  for (size_t at : {kTenthBlockAt + 20, size_t(0x20), size_t(0)}) {
    std::vector<uint8_t> bad = sealed;
    bad[at] ^= 0x01;
    EXPECT(!BodySumMatches(bad.data(), end));
  }
  // The last byte the sum covers, and the first it does not (its own).
  std::vector<uint8_t> bad = sealed;
  bad[end - 9] ^= 0x01;
  EXPECT(!BodySumMatches(bad.data(), end));
  // The stored sum itself other, in each of its bytes.
  for (size_t i = 0; i < 4; i++) {
    bad = sealed;
    bad[end - 8 + i] ^= 0x01;
    EXPECT(!BodySumMatches(bad.data(), end));
  }
  // Two bytes swapped keep a sum: what it cannot tell.
  bad = sealed;
  std::swap(bad[kTenthBlockAt + 20], bad[0x20]);
  EXPECT(BodySumMatches(bad.data(), end));
}

void TestSealedTrailer() {
  using save_file::IsSealedTrailer;
  const std::vector<uint8_t> t = SealedTrailer();
  EXPECT(IsSealedTrailer(Exact(t.data(), t.size()).get(), uint32_t(t.size())));
  // Its size, exactly.
  const size_t not_sealed[] = {0, 4, 11, 12, 13, 0x128, kSealedSize - 1, kSealedSize + 1};
  for (size_t size : not_sealed) {
    const std::vector<uint8_t> other = SealedTrailer(size);
    EXPECT(!IsSealedTrailer(Exact(other.data(), size).get(), uint32_t(size)));
  }
  // Each fixed field: the head's size, each of the 8 bytes after it, the
  // size of what was sealed.
  for (size_t at : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x124, 0x125, 0x126, 0x127}) {
    std::vector<uint8_t> bad = t;
    bad[at] ^= 0x40;
    EXPECT(!IsSealedTrailer(bad.data(), uint32_t(bad.size())));
  }
  // The sealed bytes themselves are anything.
  std::vector<uint8_t> any = t;
  for (size_t at = 0x128; at < kSealedSize; at++) any[at] = uint8_t(at);
  EXPECT(IsSealedTrailer(any.data(), uint32_t(any.size())));
}

save_file::Save ClassifyFile(const std::vector<uint8_t>& file, const std::vector<uint32_t>& game) {
  return save_file::Classify(Exact(file.data(), file.size()).get(), uint32_t(file.size()), game.data());
}

void TestClassify() {
  using save_file::Kind;
  const std::vector<uint32_t> game = GameBlocks();
  const std::vector<uint8_t> d = Data(game);
  // This game's blocks with the 12 plain bytes.
  EXPECT(ClassifyFile(File(d, PlainTrailer()), game).kind == Kind::kPlain);
  // With Live's trailer: sealed, and its sum right.
  save_file::Save s = ClassifyFile(File(d, SealedTrailer()), game);
  EXPECT(s.kind == Kind::kSealed && s.sum_ok);
  // Sealed with a byte other in a block's body: still sealed, its sum wrong.
  std::vector<uint8_t> hit = File(d, SealedTrailer());
  hit[kTenthBlockAt + 20] ^= 0x01;
  s = ClassifyFile(hit, game);
  EXPECT(s.kind == Kind::kSealed && !s.sum_ok);
  // A trailer of any other size: damaged. Around the plain one's size and
  // the sealed one's, none, and a plain one with a byte after it.
  const size_t neither[] = {0, 1, 4, 11, 13, 100, kSealedSize - 1, kSealedSize + 1};
  for (size_t size : neither)
    EXPECT(ClassifyFile(File(d, SealedTrailer(size)), game).kind == Kind::kDamaged);
  std::vector<uint8_t> stray = PlainTrailer();
  stray.push_back(0);
  EXPECT(ClassifyFile(File(d, stray), game).kind == Kind::kDamaged);
  // 360 bytes that are not Live's.
  EXPECT(ClassifyFile(File(d, std::vector<uint8_t>(kSealedSize, 0x5A)), game).kind == Kind::kDamaged);
  // A save of another version: the tenth block larger, the save sound by
  // its own sizes, with any trailer. Which block, and both sizes.
  const std::vector<uint8_t> other = Data(GameBlocks(kTenthBlockOther));
  for (const std::vector<uint8_t>& trailer : {std::vector<uint8_t>(), PlainTrailer(), SealedTrailer()}) {
    s = ClassifyFile(File(other, trailer), game);
    EXPECT(s.kind == Kind::kOtherVersion);
    EXPECT(s.block == 9 && s.block_size == kTenthBlockOther && s.game_block_size == kTenthBlock);
  }
  // The first block that differs is the one named: here the third too.
  std::vector<uint32_t> two = GameBlocks(kTenthBlockOther);
  two[2] += 7;
  s = ClassifyFile(File(Data(two), PlainTrailer()), game);
  EXPECT(s.kind == Kind::kOtherVersion && s.block == 2 && s.block_size == game[2] + 7 && s.game_block_size == game[2]);
  // And the last block.
  std::vector<uint32_t> last = GameBlocks();
  last[31] = 10;
  s = ClassifyFile(File(Data(last), PlainTrailer()), game);
  EXPECT(s.kind == Kind::kOtherVersion && s.block == 31 && s.block_size == 10 && s.game_block_size == 9);
  // This game's save with a hit in what it is laid out by: damaged, not
  // another version. In SAVE, a BLOCK, a block's size, END.
  const std::vector<uint8_t> plain = File(d, PlainTrailer());
  for (size_t at : {size_t(0xC), kTenthBlockAt, kTenthBlockAt + 5, kTenthBlockAt + 6, d.size() - 4}) {
    std::vector<uint8_t> bad = plain;
    bad[at] ^= 0x04;
    EXPECT(ClassifyFile(bad, game).kind == Kind::kDamaged);
  }
  // Cut short, and nothing at all.
  const std::vector<uint8_t> cut(plain.begin(), plain.begin() + kTenthBlockAt + 20);
  EXPECT(ClassifyFile(cut, game).kind == Kind::kDamaged);
  EXPECT(ClassifyFile({}, game).kind == Kind::kDamaged);
}

// What the game's comparison is given, row by row. The checksum and the
// trailer's first 4 bytes here are any two numbers that differ.
void TestDecide() {
  using save_file::Decide;
  using save_file::Kind;
  using save_file::Line;
  constexpr uint32_t kCrc = 0xA2F4AD56, kWord = 0x00000128;
  const auto is = [](save_file::Decision d, uint32_t checksum, Line line) {
    return d.checksum == checksum && d.line == line;
  };
  // The one case the comparison is passed: sealed, the sum right, the
  // switch on. The game is given the trailer's own bytes.
  EXPECT(is(Decide(true, Kind::kSealed, true, kCrc, kWord), kWord, Line::kSealedLoaded));
  // Sealed with the sum wrong: the game's own checksum, refused.
  EXPECT(is(Decide(true, Kind::kSealed, false, kCrc, kWord), kCrc, Line::kSealedDamaged));
  // The switch off: the game's own checksum whatever the sum.
  EXPECT(is(Decide(false, Kind::kSealed, true, kCrc, kWord), kCrc, Line::kSealedSwitchOff));
  EXPECT(is(Decide(false, Kind::kSealed, false, kCrc, kWord), kCrc, Line::kSealedSwitchOff));
  // Every other kind, the switch on or off, the sum's flag either way: the
  // game's own checksum, and why it will not match.
  for (bool on : {true, false}) {
    for (bool sum : {true, false}) {
      EXPECT(is(Decide(on, Kind::kPlain, sum, kCrc, kWord), kCrc, Line::kChecksumWrong));
      EXPECT(is(Decide(on, Kind::kOtherVersion, sum, kCrc, kWord), kCrc, Line::kOtherVersion));
      EXPECT(is(Decide(on, Kind::kDamaged, sum, kCrc, kWord), kCrc, Line::kDamaged));
      // The checksum is the trailer's: the game loads the save, nothing to say.
      EXPECT(is(Decide(on, Kind::kPlain, sum, kCrc, kCrc), kCrc, Line::kNone));
      EXPECT(is(Decide(on, Kind::kOtherVersion, sum, kCrc, kCrc), kCrc, Line::kNone));
      EXPECT(is(Decide(on, Kind::kDamaged, sum, kCrc, kCrc), kCrc, Line::kNone));
    }
    // With the switch off the game is given its checksum in every row.
    for (Kind kind : {Kind::kPlain, Kind::kSealed, Kind::kOtherVersion, Kind::kDamaged})
      for (bool sum : {true, false}) EXPECT(Decide(false, kind, sum, kCrc, on ? kWord : kCrc).checksum == kCrc);
  }
  EXPECT(is(Decide(false, Kind::kSealed, true, kCrc, kCrc), kCrc, Line::kNone));
  EXPECT(is(Decide(true, Kind::kSealed, false, kCrc, kCrc), kCrc, Line::kNone));
}

}  // namespace

int main() {
  TestErrorText();
  TestErrorHasFile();
  TestSmallestSave();
  TestWalk();
  TestBodySum();
  TestSealedTrailer();
  TestClassify();
  TestDecide();
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("save_file_test: ok");
  return 0;
}
