// The game's command line: the port's defaults, the player's
// commandline.txt and the launcher's own arguments (game_args.h).

#include "game_args.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

namespace gtaiv {

namespace {

// FUN_00455D60's buffer (0x80000 bytes at 019F2030, one byte kept for the
// terminator) and its table of 0x8000 arguments, argv[0] and the last
// entry not available to the file.
constexpr size_t kBufferBytes = 0x80000;
constexpr size_t kMaxArgs = 0x8000 - 2;
constexpr int kMaxDepth = 8;

bool Space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// FUN_00455BB0's loop over one file's text; *budget: what is left of the
// buffer, for the files it includes.
void Split(std::string_view text, const ReadFile& read, int depth, size_t* budget,
           std::vector<std::string>* out) {
  if (text.size() > *budget - 1) text = text.substr(0, *budget - 1);
  *budget -= text.size() + 1;
  // (The buffer is a C string: it ends at a zero byte.)
  text = text.substr(0, text.find('\0'));
  if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  size_t i = 0;
  const size_t n = text.size();
  while (out->size() < kMaxArgs) {
    while (i < n && Space(text[i])) i++;
    if (i == n) return;
    const char c = text[i];
    if (c == '"') {
      // To the next quote, or to the end with none.
      const size_t end = text.find('"', i + 1);
      out->emplace_back(text.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1));
      if (end == std::string_view::npos) return;
      i = end + 1;
    } else if (c == ';' || c == '#' || (c == '/' && i + 1 < n && text[i + 1] == '/')) {
      while (i < n && text[i] != '\n') i++;
    } else {
      const size_t start = i;
      while (i < n && !Space(text[i])) i++;
      std::string arg(text.substr(start, i - start));
      if (arg[0] != '@') {
        out->push_back(std::move(arg));
      } else if (std::string inner;
                 depth < kMaxDepth && *budget > 1 && read && read(arg.substr(1), *budget - 1, &inner)) {
        Split(inner, read, depth + 1, budget, out);
      }
    }
  }
}

// A regular file's first max_bytes at most (not a folder, nor a pipe that
// would wait forever).
bool ReadAtMost(const std::string& path, size_t max_bytes, std::string* text) {
  struct stat st;
  if (path.empty() || stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  text->resize(max_bytes);
  f.read(text->data(), std::streamsize(max_bytes));
  text->resize(size_t(f.gcount()));
  return !f.bad();
}

}  // namespace

std::vector<std::string> SplitResponseText(std::string_view text, const ReadFile& read) {
  std::vector<std::string> out;
  size_t budget = kBufferBytes;
  Split(text, read, 0, &budget, &out);
  return out;
}

std::string FindGamePath(const std::string& game_dir, const std::string& name) {
  if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return {};
  std::string path = game_dir;
  size_t start = 0;
  while (start < name.size()) {
    size_t end = name.find_first_of("/\\", start);
    if (end == std::string::npos) end = name.size();
    const std::string part = name.substr(start, end - start);
    start = end + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") return {};
    // The name on disk: the same one if there (tried first: no listing,
    // and a folder that can be searched but not listed still works), else
    // one that differs only in case.
    struct stat st;
    if (lstat((path + "/" + part).c_str(), &st) == 0) {
      path += "/" + part;
      continue;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) return {};
    std::string found;
    while (const dirent* e = readdir(dir)) {
      if (!strcasecmp(e->d_name, part.c_str())) {
        found = e->d_name;
        break;
      }
    }
    closedir(dir);
    if (found.empty()) return {};
    path += "/" + found;
  }
  return path == game_dir ? std::string() : path;
}

std::vector<std::string> ReadCommandLineFile(const std::string& game_dir) {
  std::string text;
  if (!ReadAtMost(FindGamePath(game_dir, "commandline.txt"), kBufferBytes - 1, &text)) return {};
  // An include names a file in the game folder (the game's current
  // directory); one outside it is not followed.
  return SplitResponseText(text, [&](const std::string& name, size_t max_bytes, std::string* inner) {
    return ReadAtMost(FindGamePath(game_dir, name), max_bytes, inner);
  });
}

std::vector<std::string> DefaultGameArgs() {
  const char* vm = std::getenv("GTAIV_VIDMEM");
  return {"-norestrictions", "-availablevidmem", vm && *vm ? vm : "2047"};
}

std::vector<std::string> LauncherGameArgs(const std::string& game_dir,
                                          const std::vector<std::string>& last) {
  std::vector<std::string> args = DefaultGameArgs();
  for (std::string& a : ReadCommandLineFile(game_dir)) args.push_back(std::move(a));
  args.insert(args.end(), last.begin(), last.end());
  return args;
}

// The CRT's rules (parse_cmdline): 2n backslashes before a quote are n
// and the quote opens or closes; 2n+1 are n and a literal quote; other
// backslashes are themselves.
std::string QuoteArg(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      backslashes++;
      continue;
    }
    out.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
    out += c;
    backslashes = 0;
  }
  out.append(backslashes * 2, '\\');
  out += '"';
  return out;
}

std::string JoinArgs(const std::vector<std::string>& args) {
  std::string line;
  for (const std::string& a : args) {
    if (!line.empty()) line += ' ';
    line += QuoteArg(a);
  }
  return line;
}

}  // namespace gtaiv
