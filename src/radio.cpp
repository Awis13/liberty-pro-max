// radio.cpp: the radio songs the 2018 update cut, restored from the Radio
// Restoration mod payload (Tomasak / Fusion Team) imported by
// tools/radio_import.sh into DataDir()/drive_c/radio-restored. The install
// is never touched: GTAIV_RADIO=1 (the launcher's "original radio songs"
// option) points the game's reads at the payload instead, the way the mod
// needs FusionFix's overloader to do on Windows.
//
// Every file the game asks for below its install directory is looked up in
// the payload, case-insensitively like NTFS, and read from there when it is
// present: the audio configs (the song table, the archive list, the
// restored waves archive) shadow the install's. The payload's "RR" text
// files (americanRR.gxt and the like, about 10 KB of the restored songs'
// names) are additions FusionFix loads next to the game's own text on
// Windows, not replacements: served in place of american.gxt (4.9 MB, every
// string of the menus and missions) they left the front end without its
// text. They are not used here.
//
// GTAIV_RADIO_LOG=1: one line per file served this way.

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "runtime/platform.h"
#include "runtime/runtime.h"

namespace {

// Install-relative path (lowercase, '/' separated) -> the file's path below
// C:\ (guest form) in the payload. Built once at startup, before any guest
// thread runs, and read-only afterwards.
std::map<std::string, std::string> g_files;
bool g_log = false;

std::string Lower(std::string s) {
  for (char& ch : s)
    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
  return s;
}

// The port's per-user data directory, mirroring the runtime's DataDir()
// with this game's id ("gtaiv", main.cpp): that one reads g_proc, which
// does not exist yet when main wires the config up.
std::string DataRoot() {
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
    return std::string(x) + "/gtaiv";
  if (const char* h = std::getenv("HOME"); h && *h)
    return std::string(h) + recomp::platform::kUserDataDir + "gtaiv";
  return "/tmp/gtaiv";
}

void Scan(const std::string& root, const std::string& rel, std::map<std::string, std::string>& out) {
  DIR* d = opendir(root.c_str());
  if (!d) return;
  struct stat st;
  while (dirent* e = readdir(d)) {
    if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
    const std::string path = root + "/" + e->d_name;
    const std::string r = rel.empty() ? std::string(e->d_name) : rel + "/" + e->d_name;
    if (stat(path.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode))
      Scan(path, r, out);
    else if (S_ISREG(st.st_mode))
      out[Lower(r)] = "C:/radio-restored/" + r;
  }
  closedir(d);
}

}  // namespace

// Called from main.cpp when GTAIV_RADIO is set, before the game starts.
void GtaivRadioInit() {
  g_log = [] {
    const char* e = std::getenv("GTAIV_RADIO_LOG");
    return e && *e && *e != '0';
  }();
  const std::string root = DataRoot() + "/drive_c/radio-restored";
  Scan(root, "", g_files);
  if (g_files.empty())
    std::fprintf(stderr, "radio: no files under %s: import them with tools/radio_import.sh\n", root.c_str());
  else
    std::fprintf(stderr, "radio: %zu restored files under %s\n", g_files.size(), root.c_str());
}

// GameConfig::read_redirect: the file to read in place of this guest path,
// or "" for the file itself. Paths arrive absolute and normalised (the
// runtime's AbsGuest).
std::string GtaivRadioRedirect(const std::string& guest_path) {
  const std::string& install = recomp::Game().guest_dir;
  const std::string lg = Lower(guest_path), li = Lower(install);
  if (lg.size() <= li.size() || lg.compare(0, li.size(), li) != 0 || guest_path[li.size()] != '\\')
    return {};
  std::string key = guest_path.substr(li.size() + 1);
  for (char& ch : key)
    if (ch == '\\') ch = '/';
  key = Lower(key);
  auto it = g_files.find(key);
  if (it == g_files.end()) return {};
  if (g_log) std::fprintf(stderr, "radio: %s -> %s\n", guest_path.c_str(), it->second.c_str());
  return it->second;
}
