// game_args (src/game_args.h): commandline.txt read as the game reads it,
// and the order of the game's arguments. Files go to a scratch folder in
// the current directory (ctest: the build folder), removed at the end.

#include "game_args.h"

#include <stdlib.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

using Args = std::vector<std::string>;

std::string Show(const Args& args) {
  std::string s = "{";
  for (size_t i = 0; i < args.size(); i++) s += (i ? ", [" : "[") + args[i] + "]";
  return s + "}";
}

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "game_args_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

void ExpectArgs(const Args& got, const Args& want, int line) {
  if (got == want) return;
  std::fprintf(stderr, "game_args_test.cpp:%d: FAILED: got %s, want %s\n", line, Show(got).c_str(),
               Show(want).c_str());
  g_failures++;
}
#define EXPECT_ARGS(got, ...) ExpectArgs((got), Args(__VA_ARGS__), __LINE__)

// The value the game's sysParam (FUN_00461DB0) ends up with for "-name":
// every occurrence overwrites the one before; the value is the next
// argument unless that is another "-name" (a '-' not before a digit).
std::string GameValue(const Args& args, const std::string& name) {
  std::string value = "(unset)";
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] != "-" + name) continue;
    const bool next = i + 1 < args.size() &&
                      !(args[i + 1][0] == '-' && !(args[i + 1].size() > 1 && isdigit(args[i + 1][1])));
    value = next ? args[i + 1] : "";
  }
  return value;
}

// The game's CRT (parse_cmdline, 00CF4917) splitting a command line.
Args CrtSplit(const std::string& line) {
  Args out;
  const char* p = line.c_str();
  for (;;) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    std::string arg;
    bool in_quote = false;
    for (;;) {
      bool copy = true;
      unsigned slashes = 0;
      while (*p == '\\') p++, slashes++;
      if (*p == '"') {
        if (slashes % 2 == 0) {
          if (in_quote && p[1] == '"') {
            p++;
          } else {
            copy = false;
            in_quote = !in_quote;
          }
        }
        slashes /= 2;
      }
      arg.append(slashes, '\\');
      if (!*p || (!in_quote && (*p == ' ' || *p == '\t'))) break;
      if (copy) arg += *p;
      p++;
    }
    out.push_back(arg);
  }
  return out;
}

struct Scratch {
  std::filesystem::path dir;
  Scratch() {
    char name[] = "game_args_test.XXXXXX";
    dir = std::filesystem::absolute(mkdtemp(name));
  }
  ~Scratch() { std::filesystem::remove_all(dir); }
  void Write(const std::string& name, const std::string& text) const {
    std::ofstream(dir / name, std::ios::binary) << text;
  }
  Args Read() const { return gtaiv::ReadCommandLineFile(dir.string()); }
};

void TestNoFile() {
  Scratch s;
  EXPECT_ARGS(s.Read(), {});
  unsetenv("GTAIV_VIDMEM");
  EXPECT_ARGS(gtaiv::LauncherGameArgs(s.dir.string(), {}), {"-norestrictions", "-availablevidmem", "2047"});
  EXPECT_ARGS(gtaiv::LauncherGameArgs((s.dir / "missing").string(), {"-width", "800"}),
              {"-norestrictions", "-availablevidmem", "2047", "-width", "800"});
}

void TestEmptyFiles() {
  Scratch s;
  s.Write("commandline.txt", "");
  EXPECT_ARGS(s.Read(), {});
  s.Write("commandline.txt", " \t\r\n\r\n  ");
  EXPECT_ARGS(s.Read(), {});
}

void TestLines() {
  Scratch s;
  // The usual file, and the one players wrote for the texture quality.
  s.Write("commandline.txt", "-norestrictions -availablevidmem 2047\n");
  EXPECT_ARGS(s.Read(), {"-norestrictions", "-availablevidmem", "2047"});
  s.Write("commandline.txt", "-availablevidmem 2048 -nomemrestrict -norestrictions");
  EXPECT_ARGS(s.Read(), {"-availablevidmem", "2048", "-nomemrestrict", "-norestrictions"});
  s.Write("commandline.txt", "-norestrictions\n-availablevidmem 3000\n-fullscreen\n");
  EXPECT_ARGS(s.Read(), {"-norestrictions", "-availablevidmem", "3000", "-fullscreen"});
}

void TestWhitespace() {
  Scratch s;
  s.Write("commandline.txt", "  \t-norestrictions\t\t-availablevidmem   1024 \r\n\r\n\t-frameLimit 60\r\n   ");
  EXPECT_ARGS(s.Read(), {"-norestrictions", "-availablevidmem", "1024", "-frameLimit", "60"});
  // A CR alone separates too.
  s.Write("commandline.txt", "-a\r-b");
  EXPECT_ARGS(s.Read(), {"-a", "-b"});
}

void TestNoTrailingNewline() {
  Scratch s;
  s.Write("commandline.txt", "-norestrictions -availablevidmem 2047");
  EXPECT_ARGS(s.Read(), {"-norestrictions", "-availablevidmem", "2047"});
}

void TestQuotes() {
  using gtaiv::SplitResponseText;
  EXPECT_ARGS(SplitResponseText("-name \"two words\" -x"), {"-name", "two words", "-x"});
  // No escapes: a backslash is itself; the next quote ends it.
  EXPECT_ARGS(SplitResponseText("\"C:\\dir\\\" -x"), {"C:\\dir\\", "-x"});
  // A quote only opens an argument at its start.
  EXPECT_ARGS(SplitResponseText("-a\"b c\""), {"-a\"b", "c\""});
  // Right after the closing quote a new argument starts.
  EXPECT_ARGS(SplitResponseText("\"a\"b"), {"a", "b"});
  // Without a closing quote it runs to the end, lines and all.
  EXPECT_ARGS(SplitResponseText("-x \"open\n-y"), {"-x", "open\n-y"});
  EXPECT_ARGS(SplitResponseText("-x \"\""), {"-x", ""});
  EXPECT_ARGS(SplitResponseText("-x \""), {"-x", ""});
}

void TestComments() {
  using gtaiv::SplitResponseText;
  EXPECT_ARGS(SplitResponseText("; settings\n-a\n# more\r\n-b // trailing\n//-c\n-d"), {"-a", "-b", "-d"});
  // Only at the start of an argument.
  EXPECT_ARGS(SplitResponseText("-a;b -c#d e/f /g"), {"-a;b", "-c#d", "e/f", "/g"});
  // To the end of the line, also at the end of the file.
  EXPECT_ARGS(SplitResponseText("-a ;b c"), {"-a"});
}

void TestEndOfText() {
  using gtaiv::SplitResponseText;
  // A zero byte ends the game's buffer.
  EXPECT_ARGS(SplitResponseText(std::string_view("-a\0-b", 5)), {"-a"});
  // A UTF-8 byte order mark is not part of the first argument.
  EXPECT_ARGS(SplitResponseText("\xEF\xBB\xBF-norestrictions -x"), {"-norestrictions", "-x"});
}

void TestLongLines() {
  using gtaiv::SplitResponseText;
  std::string line, big(200000, 'v');
  Args want;
  for (int i = 0; i < 5000; i++) {
    line += "-option" + std::to_string(i) + ' ';
    want.push_back("-option" + std::to_string(i));
  }
  line += "-big " + big;
  want.push_back("-big");
  want.push_back(big);
  EXPECT_ARGS(SplitResponseText(line), want);
  // Past the game's 512 KB buffer the rest is not read.
  const std::string huge = "-a " + std::string(0x90000, 'x') + " -b";
  const Args got = SplitResponseText(huge);
  EXPECT(got.size() == 2 && got[0] == "-a" && got[1].size() == 0x80000 - 1 - 3);
  // At most 0x7FFE arguments.
  std::string many;
  for (int i = 0; i < 0x8100; i++) many += "-a ";
  EXPECT(SplitResponseText(many).size() == 0x7FFE);
}

void TestIncludes() {
  Scratch s;
  std::filesystem::create_directory(s.dir / "cfg");
  s.Write("cfg/more.txt", "-b \"x y\"\n@cfg\\last.txt");
  s.Write("cfg/last.txt", "-c");
  s.Write("commandline.txt", "-a @cfg\\more.txt -d @missing.txt \"@cfg/last.txt\" @C:\\abs.txt -e");
  EXPECT_ARGS(s.Read(), {"-a", "-b", "x y", "-c", "-d", "@cfg/last.txt", "-e"});
  // A file that includes itself stops at 8 deep.
  s.Write("loop.txt", "-l @loop.txt");
  s.Write("commandline.txt", "@loop.txt");
  EXPECT(s.Read() == Args(8, "-l"));
}

void TestEncodings() {
  Scratch s;
  // ANSI (Windows-1252) en dashes, which the game takes for '-': the bytes
  // go through untouched, to the game's own command line and back.
  const std::string dash = "\x96norestrictions \x96" "availablevidmem 3000 \"caf\xE9 \x93q\x94\"\n";
  s.Write("commandline.txt", dash);
  const Args file = {"\x96norestrictions", "\x96" "availablevidmem", "3000", "caf\xE9 \x93q\x94"};
  EXPECT_ARGS(s.Read(), file);
  unsetenv("GTAIV_VIDMEM");
  const Args all = gtaiv::LauncherGameArgs(s.dir.string(), {});
  EXPECT_ARGS(Args(all.begin() + 3, all.end()), file);
  const Args back = CrtSplit("GTAIV.exe " + gtaiv::JoinArgs(all));
  EXPECT_ARGS(Args(back.begin() + 1, back.end()), all);
  // UTF-16: the game's buffer ends at the first zero byte, one argument of
  // junk (not an option) before it.
  s.Write("commandline.txt", std::string("\xFF\xFE-\0n\0o\0r\0e\0s\0\n\0", 16));
  EXPECT_ARGS(s.Read(), {"\xFF\xFE-"});
  s.Write("commandline.txt", std::string("-\0n\0o\0r\0", 8));
  EXPECT_ARGS(s.Read(), {"-"});
}

void TestCase() {
  Scratch s;
  // The game's lookups ignore case (as on NTFS), on any volume.
  std::filesystem::create_directory(s.dir / "Cfg");
  s.Write("CommandLine.TXT", "-a @Extra.txt @cfg\\MORE.txt");
  s.Write("extra.TXT", "-b");
  s.Write("Cfg/more.Txt", "-c");
  EXPECT_ARGS(s.Read(), {"-a", "-b", "-c"});
  // The names on disk, found by their spelling in any case.
  EXPECT(gtaiv::FindGamePath(s.dir.string(), "EXTRA.txt") == (s.dir / "extra.TXT").string());
  EXPECT(gtaiv::FindGamePath(s.dir.string(), "cfg\\more.TXT") == (s.dir / "Cfg/more.Txt").string());
  EXPECT(gtaiv::FindGamePath(s.dir.string(), "./cfg//more.txt") == (s.dir / "Cfg/more.Txt").string());
  EXPECT(gtaiv::FindGamePath(s.dir.string(), "missing.txt").empty());
  EXPECT(gtaiv::FindGamePath(s.dir.string(), "cfg/missing/more.txt").empty());
  // The same name in two spellings: the exact one.
  s.Write("Twin.txt", "1");
  s.Write("twin.txt", "2");
  const std::string twin = gtaiv::FindGamePath(s.dir.string(), "twin.txt");
  EXPECT(twin == (s.dir / "twin.txt").string() || twin == (s.dir / "Twin.txt").string());
}

void TestOutsideTheGameFolder() {
  Scratch s;
  const std::filesystem::path game = s.dir / "game";
  std::filesystem::create_directory(game);
  std::filesystem::create_directory(game / "cfg");
  s.Write("secret.txt", "-secret");
  s.Write("game/commandline.txt", "@..\\secret.txt @cfg/../../secret.txt @../secret.txt @" + (s.dir / "secret.txt").string() +
                                      " @C:\\secret.txt @\\secret.txt -x");
  EXPECT_ARGS(gtaiv::ReadCommandLineFile(game.string()), {"-x"});
  EXPECT(gtaiv::FindGamePath(game.string(), "../secret.txt").empty());
  EXPECT(gtaiv::FindGamePath(game.string(), "cfg\\..\\commandline.txt").empty());
  EXPECT(gtaiv::FindGamePath(game.string(), "/etc/hosts").empty());
  EXPECT(gtaiv::FindGamePath(game.string(), "C:commandline.txt").empty());
  EXPECT(gtaiv::FindGamePath(game.string(), "").empty());
  // A folder is not a file to read.
  s.Write("game/commandline.txt", "@cfg -y");
  EXPECT_ARGS(gtaiv::ReadCommandLineFile(game.string()), {"-y"});
}

void TestReadsStopAtTheBuffer() {
  // An include is asked for no more than what is left of the game's 512 KB.
  size_t asked = 0;
  const Args got = gtaiv::SplitResponseText("-a @x", [&](const std::string& name, size_t max, std::string* text) {
    asked = max;
    *text = name == "x" ? "-b" : "";
    return true;
  });
  EXPECT_ARGS(got, {"-a", "-b"});
  EXPECT(asked == 0x80000 - 6 - 1);
  // Files on disk: commandline.txt and an include cut there.
  Scratch s;
  s.Write("commandline.txt", "-a " + std::string(0x100000, 'x'));
  Args read = s.Read();
  EXPECT(read.size() == 2 && read[1] == std::string(0x80000 - 1 - 3, 'x'));
  s.Write("commandline.txt", "-a @big.rpf");
  s.Write("big.rpf", std::string(0x100000, 'y'));
  read = s.Read();
  EXPECT(read.size() == 2 && read[1] == std::string(0x80000 - 12 - 1, 'y'));
}

void TestOrder() {
  Scratch s;
  unsetenv("GTAIV_VIDMEM");
  s.Write("commandline.txt", "-availablevidmem 4096 -width 800 -height 600\n");
  const Args args = gtaiv::LauncherGameArgs(s.dir.string(), {"-width", "1920", "-height", "1080"});
  EXPECT_ARGS(args, {"-norestrictions", "-availablevidmem", "2047", "-availablevidmem", "4096", "-width", "800",
                     "-height", "600", "-width", "1920", "-height", "1080"});
  // The player's video memory over the default; the launcher's resolution
  // over the player's.
  EXPECT(GameValue(args, "availablevidmem") == "4096");
  EXPECT(GameValue(args, "width") == "1920");
  EXPECT(GameValue(args, "height") == "1080");
  EXPECT(GameValue(args, "norestrictions") == "");
  // Arguments after "--" come last of all (the Mac passes them as `last`).
  const Args mac = gtaiv::LauncherGameArgs(s.dir.string(), {"-availablevidmem", "1500"});
  EXPECT(GameValue(mac, "availablevidmem") == "1500");
}

void TestVideoMemoryVariable() {
  setenv("GTAIV_VIDMEM", "1536", 1);
  EXPECT_ARGS(gtaiv::DefaultGameArgs(), {"-norestrictions", "-availablevidmem", "1536"});
  setenv("GTAIV_VIDMEM", "", 1);
  EXPECT_ARGS(gtaiv::DefaultGameArgs(), {"-norestrictions", "-availablevidmem", "2047"});
  unsetenv("GTAIV_VIDMEM");
  EXPECT_ARGS(gtaiv::DefaultGameArgs(), {"-norestrictions", "-availablevidmem", "2047"});
}

void TestQuoting() {
  using gtaiv::QuoteArg;
  EXPECT(QuoteArg("-norestrictions") == "-norestrictions");
  EXPECT(QuoteArg("C:\\dir\\file") == "C:\\dir\\file");
  EXPECT(QuoteArg("") == "\"\"");
  EXPECT(QuoteArg("two words") == "\"two words\"");
  EXPECT(QuoteArg("dir\\ with\\") == "\"dir\\ with\\\\\"");
  // Every argument comes back from the game's CRT as it went in.
  const Args tricky = {"-a", "", "two words", "tab\there", "quote\"in", "\"", "\\", "\\\\", "end\\",
                       "sp ace\\", "a\\\"b", "a\\\\\"b", "x\\\\", "line\nbreak", "C:\\Program Files\\x\\"};
  const Args back = CrtSplit("\"C:\\Program Files\\Rockstar Games\\Grand Theft Auto IV\\GTAIV.exe\" " +
                             gtaiv::JoinArgs(tricky));
  EXPECT(back.size() == tricky.size() + 1);
  EXPECT_ARGS(Args(back.begin() + 1, back.end()), tricky);
  EXPECT(gtaiv::JoinArgs({}).empty());
  EXPECT(gtaiv::JoinArgs({"", "-a"}) == "\"\" -a");
}

}  // namespace

int main() {
  TestNoFile();
  TestEmptyFiles();
  TestLines();
  TestWhitespace();
  TestNoTrailingNewline();
  TestQuotes();
  TestComments();
  TestEndOfText();
  TestLongLines();
  TestIncludes();
  TestEncodings();
  TestCase();
  TestOutsideTheGameFolder();
  TestReadsStopAtTheBuffer();
  TestOrder();
  TestVideoMemoryVariable();
  TestQuoting();
  if (g_failures) {
    std::fprintf(stderr, "game_args_test: %d failed\n", g_failures);
    return 1;
  }
  std::printf("game_args_test: all passed\n");
  return 0;
}
