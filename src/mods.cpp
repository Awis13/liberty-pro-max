// mods.cpp: the player's mods, from DataDir()/drive_c/mods. Each folder there
// is a mod, its files laid out as in the game's folder (pc/..., common/...),
// anywhere inside it (a leading update/, as many mods carry for FusionFix's
// loader, a mod pack's folders); its .img archives are added to the game's, and
// what the update folder leaves out is left out here too (update_folder.cpp:
// the episodes' folders, FusionFix's shaders while they are off...). The game's
// reads of its install are pointed at the files, case-insensitively like NTFS;
// of two mods with the same file the later name wins, and a mod's files win
// over the update folder's. The install is never touched. GTAIV_MODS=0 turns
// it off; GTAIV_MODS_LOG=1 logs one line per file served this way.

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "option.h"
#include "runtime/platform.h"
#include "runtime/runtime.h"

namespace recomp::gen {
std::string UpdateFolderAddModFile(const std::string& rel, const std::string& guest);  // update_folder.cpp
void UpdateFolderEndMod();
bool UpdateFolderEpisodeData(const std::string& rel);
}  // namespace recomp::gen

namespace {

// Install-relative path (lowercase, '/' separated) -> the file's path below
// C:\ (guest form) in its mod. Built once at startup, before any guest
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

// The files below `dir` (rel: its path in the mod, guest: the mod's folder).
void Scan(const std::string& dir, const std::string& rel, const std::string& guest) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct stat st;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    const std::string path = dir + "/" + e->d_name;
    const std::string r = rel.empty() ? std::string(e->d_name) : rel + "/" + e->d_name;
    if (stat(path.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      Scan(path, r, guest);
    } else if (S_ISREG(st.st_mode)) {
      std::string in_mod = r;
      if (Lower(r).rfind("update/", 0) == 0) in_mod.erase(0, 7);
      const std::string key = recomp::gen::UpdateFolderAddModFile(in_mod, guest + r);
      if (!key.empty()) g_files[key] = guest + r;
    }
  }
  closedir(d);
}

}  // namespace

// Called from main.cpp before the game starts.
void GtaivModsInit() {
  g_log = [] {
    const char* e = std::getenv("GTAIV_MODS_LOG");
    return e && *e && *e != '0';
  }();
  if (!OptionOn("GTAIV_MODS", true)) return;
  const std::string root = DataRoot() + "/drive_c/mods";
  std::vector<std::string> mods;
  if (DIR* d = opendir(root.c_str())) {
    while (dirent* e = readdir(d))
      if (e->d_name[0] != '.') mods.push_back(e->d_name);
    closedir(d);
  }
  std::sort(mods.begin(), mods.end());
  for (const std::string& m : mods) {
    Scan(root + "/" + m, "", "C:/mods/" + m + "/");
    recomp::gen::UpdateFolderEndMod();
  }
  if (!g_files.empty())
    std::fprintf(stderr, "mods: %zu files from %zu folders in %s\n", g_files.size(), mods.size(), root.c_str());
}

// GameConfig::read_redirect: the file to read in place of this guest path,
// or "" for the file itself. Paths arrive absolute and normalised (the
// runtime's AbsGuest).
std::string GtaivModsRedirect(const std::string& guest_path) {
  const std::string& install = recomp::Game().guest_dir;
  const std::string lg = Lower(guest_path), li = Lower(install);
  if (lg.size() <= li.size() || lg.compare(0, li.size(), li) != 0 || guest_path[li.size()] != '\\')
    return {};
  const std::string rel = Lower(guest_path.substr(li.size() + 1));
  std::string key = rel;
  for (char& ch : key)
    if (ch == '\\') ch = '/';
  auto it = g_files.find(key);
  if (it == g_files.end()) return {};
  if (recomp::gen::UpdateFolderEpisodeData(rel)) return {};
  if (g_log) std::fprintf(stderr, "mods: %s -> %s\n", guest_path.c_str(), it->second.c_str());
  return it->second;
}
