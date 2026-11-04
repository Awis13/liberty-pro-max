// FusionFix's data, from the player's own install of it: its "update"
// folder in the game folder (FusionFix's files are not part of the port).
// Its files are read in place of the game's own, as FusionFix's loader
// has Windows do (the folder's common\ over the game's common\, pc\ over
// pc\), and its IMG archives (its fixed mission scripts, models and
// animations) are added to the game's after its own update.img, as
// FusionFix's IMG loader does, and its texts (<language>FF.gxt, its
// scripts' and its own lines) are found before the game's, as its GXT
// loader does (by their ideas). GTAIV_FF_DATA=0 turns it off.
//
// Left out until the port can draw with them: FusionFix's shaders
// (common\shaders: they need its per-frame shader constants), and its menu
// layout (common\data\frontend_menus.xml: rows for its in-game settings,
// which the launcher has instead). The episodes' folders (TLAD, TBoGT) are
// not IV's.

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

bool FfData() {
  static const bool on = OptionOn("GTAIV_FF_DATA", true);
  return on;
}

// FusionFix's shaders too (GTAIV_FF_SHADERS, off for now: they need its
// shader constants, work in progress).
bool FfShaders() {
  static const bool on = FfData() && OptionOn("GTAIV_FF_SHADERS", false);
  return on;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}

// Names compared as Windows Explorer does: runs of digits by their value
// ("2a" before "10"), the rest without case.
bool NaturalLess(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(uint8_t(a[i])) && std::isdigit(uint8_t(b[j]))) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && std::isdigit(uint8_t(a[i2]))) i2++;
      while (j2 < b.size() && std::isdigit(uint8_t(b[j2]))) j2++;
      const unsigned long x = std::stoul(a.substr(i, i2 - i)), y = std::stoul(b.substr(j, j2 - j));
      if (x != y) return x < y;
      i = i2;
      j = j2;
      continue;
    }
    const char x = char(std::tolower(uint8_t(a[i]))), y = char(std::tolower(uint8_t(b[j])));
    if (x != y) return x < y;
    i++;
    j++;
  }
  return a.size() - i < b.size() - j;
}

// Below an episode's folder, wherever it is: not IV's files.
bool EpisodePath(const std::string& lower) {
  return lower.find("\\tlad\\") != std::string::npos || lower.find("\\tbogt\\") != std::string::npos ||
         lower.rfind("tlad\\", 0) == 0 || lower.rfind("tbogt\\", 0) == 0;
}

// A mod's own folder (<mod>\pc\...): its files count from the first of the
// game's top folders in their path.
std::string GameKey(std::string key) {
  for (size_t at = 0; at != std::string::npos;) {
    const std::string comp = key.substr(at, key.find('\\', at) - at);
    if (comp == "common" || comp == "pc" || comp == "audio" || comp == "movies") return key.substr(at);
    at = key.find('\\', at);
    if (at != std::string::npos) at++;
  }
  return key;
}

// Archives come in the order FusionFix loads them (a later archive's files
// win): by the folder they are in at the top (a mod's own: "0 Minor Mods",
// "1 Potential Grim"...), within it shallower first (the shared archives
// before IV's own), then by name.
bool ArchiveLess(const std::string& a, const std::string& b) {
  const std::string ta = Lower(a.substr(0, a.find('\\'))), tb = Lower(b.substr(0, b.find('\\')));
  if (ta != tb) return NaturalLess(ta, tb);
  const auto da = std::count(a.begin(), a.end(), '\\'), db = std::count(b.begin(), b.end(), '\\');
  return da != db ? da < db : NaturalLess(a, b);
}

// What mods bring beyond the game's shaders, for models that use them even
// without FusionFix's shaders (the game ends with EFC20 when a model's
// shader was never loaded): lower-case name without extension (win32_30's
// .fxc), or path (db's and dcl's files), to where it is: a guest path, or
// a path below the install. Set before the game's threads run.
std::map<std::string, std::string> g_mod_fxc, g_mod_shader_files;

void NoteShaderFile(const std::string& key, const std::string& source, bool replace) {
  const std::string dir = "common\\shaders\\";
  if (key.rfind(dir, 0) != 0) return;
  const size_t slash = key.find('\\', dir.size());
  if (slash == std::string::npos || key.find('\\', slash + 1) != std::string::npos) return;
  const std::string folder = key.substr(dir.size(), slash - dir.size());
  const std::string name = key.substr(slash + 1);
  std::map<std::string, std::string>* into = nullptr;
  std::string id;
  if (folder == "win32_30" && name.size() > 4 && name.compare(name.size() - 4, 4, ".fxc") == 0) {
    into = &g_mod_fxc;
    id = name.substr(0, name.size() - 4);
  } else if (folder == "db" || folder == "dcl") {
    into = &g_mod_shader_files;
    id = key;
  }
  if (!into) return;
  if (replace) (*into)[id] = source;
  else into->emplace(id, source);
}

// Whether the game's own folder of common\shaders has this file.
bool GameHasShaderFile(const std::string& folder, const std::string& name) {
  static std::mutex mu;
  static std::map<std::string, std::set<std::string>> listed;
  std::lock_guard<std::mutex> l(mu);
  auto it = listed.find(folder);
  if (it == listed.end()) {
    it = listed.emplace(folder, std::set<std::string>{}).first;
    std::error_code ec;
    const auto dir = std::filesystem::path(g_proc->game_dir) / "common" / "shaders" / folder;
    for (std::filesystem::directory_iterator d(dir, ec), end; !ec && d != end; d.increment(ec))
      it->second.insert(Lower(d->path().filename().string()));
  }
  return it->second.count(name) != 0;
}

// The file a mod brings for this read of the game's shader folders, without
// FusionFix's shaders: only what the game does not have itself.
std::string ModShaderFile(const std::string& rel) {
  const std::string dir = "common\\shaders\\";
  if (rel.rfind(dir, 0) != 0) return {};
  const size_t slash = rel.find('\\', dir.size());
  if (slash == std::string::npos) return {};
  const std::string folder = rel.substr(dir.size(), slash - dir.size());
  const std::string name = rel.substr(slash + 1);
  if (GameHasShaderFile(folder, name)) return {};
  if (folder.rfind("win32_30", 0) == 0 && name.size() > 4) {
    auto it = g_mod_fxc.find(name.substr(0, name.size() - 4));
    return it == g_mod_fxc.end() ? std::string{} : it->second;
  }
  auto it = g_mod_shader_files.find(rel);
  return it == g_mod_shader_files.end() ? std::string{} : it->second;
}

// The mods folder's archives (guest paths), each mod's in the order above.
std::vector<std::string> g_mod_imgs;
std::vector<std::pair<std::string, std::string>> g_this_mod_imgs;

// The update folder's files: lower-case path below it (backslashes) to
// the path as it is on disk; and its IMG archives for IV, in load order.
struct UpdateFolder {
  std::unordered_map<std::string, std::string> files;
  std::vector<std::string> imgs;
};

// GTAIV_UPDATE_EXCLUDE=prefix,prefix: files below the game's folder
// starting so are not taken (lower case, backslashes; e.g. pc\audio\),
// to find which of a mod pack's files a problem comes from.
bool Excluded(const std::string& lower_rel) {
  static const std::vector<std::string> prefixes = [] {
    std::vector<std::string> v;
    const char* e = std::getenv("GTAIV_UPDATE_EXCLUDE");
    std::string all = e ? Lower(e) : "";
    std::replace(all.begin(), all.end(), '/', '\\');
    for (size_t at = 0; at < all.size();) {
      const size_t end = std::min(all.find(',', at), all.size());
      if (end > at) v.push_back(all.substr(at, end - at));
      at = end + 1;
    }
    return v;
  }();
  for (const std::string& p : prefixes)
    if (lower_rel.rfind(p, 0) == 0) return true;
  return false;
}

bool LeftOut(const std::string& lower_rel) {
  if (Excluded(lower_rel)) return true;
  if (lower_rel.rfind("common\\shaders\\", 0) == 0 && !FfShaders()) return true;
  // Its stipple texture goes with its shaders: with the game's own, trees are
  // flat rectangles with lines across them.
  if (lower_rel == "pc\\textures\\stipple.wtd" && !FfShaders()) return true;
  // Its post-processing effect has a layout the game's loader misreads
  // (named entries after the header; the game then hands Direct3D no
  // shader): the game's own stays.
  if (lower_rel == "common\\shaders\\win32_30\\rage_postfx.fxc") return true;
  return lower_rel == "common\\data\\frontend_menus.xml" || lower_rel == "update.txt" ||
         lower_rel == "update.img";
}

const UpdateFolder& Folder() {
  static UpdateFolder f;
  static std::once_flag once;
  std::call_once(once, [] {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(g_proc->game_dir) / "update";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::follow_directory_symlink, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string rel = it->path().lexically_relative(root).string();
      std::replace(rel.begin(), rel.end(), '/', '\\');
      const std::string lower = Lower(rel);
      if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".img") == 0) {
        if (lower != "update.img") f.imgs.push_back(rel);
        continue;
      }
      if (EpisodePath(lower)) continue;
      const std::string key = GameKey(lower);
      if (!Excluded(key)) NoteShaderFile(key, "update\\" + rel, false);
      if (!LeftOut(key) && !f.files.count(key)) f.files[key] = rel;
    }
    std::sort(f.imgs.begin(), f.imgs.end(), ArchiveLess);
    WarnF("gtaiv: update folder: %zu files read over the game's, %zu archives\n", f.files.size(), f.imgs.size());
  });
  return f;
}

}  // namespace

bool FfShadersOn() { return FfShaders(); }

// One file of the mods folder (rel: its path in the mod, guest: where it is).
// Its archive is kept for the game's IMG list, and the rest is given back as
// the path it stands for in the game's folder (lower case, '/'), or "" when
// it is not for IV or not used.
std::string UpdateFolderAddModFile(const std::string& rel, const std::string& guest) {
  std::string lower = Lower(rel);
  std::replace(lower.begin(), lower.end(), '/', '\\');
  if (EpisodePath(lower)) return {};
  if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".img") == 0) {
    if (lower != "update.img") g_this_mod_imgs.emplace_back(lower, guest);
    return {};
  }
  std::string key = GameKey(lower);
  if (!Excluded(key)) NoteShaderFile(key, guest, true);
  if (LeftOut(key)) return {};
  std::replace(key.begin(), key.end(), '\\', '/');
  return key;
}

// The end of one mod's files.
void UpdateFolderEndMod() {
  std::sort(g_this_mod_imgs.begin(), g_this_mod_imgs.end(),
            [](const auto& a, const auto& b) { return ArchiveLess(a.first, b.first); });
  for (const auto& [rel, guest] : g_this_mod_imgs) g_mod_imgs.push_back(guest);
  g_this_mod_imgs.clear();
}

// GameConfig::read_redirect: a file below the install read from the update
// folder when it has one.
std::string UpdateFolderRedirect(const std::string& guest_path) {
  if (!FfData()) return {};
  const std::string& dir = Game().guest_dir;
  if (guest_path.size() <= dir.size() + 1 || Lower(guest_path.substr(0, dir.size())) != Lower(dir) ||
      guest_path[dir.size()] != '\\')
    return {};
  std::string rel = Lower(guest_path.substr(dir.size() + 1));
  std::replace(rel.begin(), rel.end(), '/', '\\');
  if (rel.rfind("update\\", 0) == 0) return {};
  const UpdateFolder& f = Folder();
  if (!FfShaders()) {
    const std::string own = ModShaderFile(rel);
    if (!own.empty()) {
      WarnF("gtaiv: mods: %s read from %s\n", rel.c_str(), own.c_str());
      return own.rfind("update\\", 0) == 0 ? dir + "\\" + own : own;
    }
  }
  auto it = f.files.find(rel);
  if (it == f.files.end()) return {};
  static std::mutex mu;
  static std::unordered_map<std::string, bool> told;
  // IV's map and game data would stop an episode, which has its own.
  if (Rd32(0x0112D788) != 0 && (rel.rfind("common\\data\\", 0) == 0 || rel.rfind("pc\\data\\maps\\", 0) == 0)) return {};
  {
    std::lock_guard<std::mutex> l(mu);
    if (!told[rel]) {
      told[rel] = true;
      WarnF("gtaiv: update folder: %s read from update\\%s\n", rel.c_str(), it->second.c_str());
    }
  }
  return dir + "\\update\\" + it->second;
}

// Whether an archive (lower case) is for the episode being started (0: IV).
// An episode's own are in its folder; the rest are IV's, with its mission
// scripts (SRCP10 in an episode), unless the file is named for the episodes
// too (EFLC).
static bool ArchiveForEpisode(const std::string& lower, uint32_t episode) {
  if (lower.find("tlad\\") != std::string::npos) return episode == 1;
  if (lower.find("tbogt\\") != std::string::npos) return episode == 2;
  return episode == 0 || lower.find("eflc", lower.rfind('\\') + 1) != std::string::npos;
}

// The start of the IMG list's reading (the function at 00AC6590, after
// its stack cookie): mov [esp+44], eax; mov eax, [esp+4C]. The update
// folder's archives go in first, each through CImgManager::addImgFile
// (00A22BE0, cdecl: path, 1, -1) as "update:/<path>".
void hook_00AC659A(Ctx& c) {
  Wr32(c.esp + 0x44, c.eax);
  const uint32_t list = Rd32(c.esp + 0x4C);
  // The episode being started; the game reads the list again for another.
  const uint32_t episode = Rd32(0x0112D788);
  static uint32_t added_for = 0xFFFFFFFFu;
  if (FfData() && added_for != episode) {
    added_for = episode;
    const auto add = [&c](const std::string& path) {
      const uint32_t room = (uint32_t(path.size()) + 1 + 15) & ~15u;
      c.esp -= room;  // the path, below the frame
      std::memcpy(GPtr(c.esp), path.c_str(), path.size() + 1);
      CallGuest(c, 0x00A22BE0, {c.esp, 1, 0xFFFFFFFFu});
      c.esp += room;
      WarnF("gtaiv: update folder: archive %s added\n", path.c_str());
    };
    for (std::string rel : Folder().imgs) {
      if (!ArchiveForEpisode(Lower(rel), episode)) continue;
      std::replace(rel.begin(), rel.end(), '\\', '/');
      add("update:/" + rel);
    }
    if (episode == 0) {
      for (const std::string& path : g_mod_imgs) add(path);  // after: a mod's files win
    }
  }
  c.eax = list;
}

// FusionFix's texts: its GXT for the game's language, by key hash, the
// strings copied into guest memory (the game draws them from there).
namespace {

std::mutex g_text_mu;
std::unordered_map<uint32_t, uint32_t> g_texts;  // key hash -> guest wide string
std::string g_text_file;                         // the GXT they came from

// A GXT of one table (version 4, 16-bit characters): TKEY (string offset,
// key hash) pairs, then TDAT, the strings.
void LoadTexts(const std::string& host) {
  std::ifstream in(host, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, d.data() + at, 4);
    return v;
  };
  g_texts.clear();
  if (d.size() < 16 || u32(0) != 0x00100004 || std::memcmp(d.data() + 4, "TKEY", 4) != 0) return;
  const uint32_t keys = u32(8);
  const size_t dat = 12 + keys;
  if (dat + 8 > d.size() || std::memcmp(d.data() + dat, "TDAT", 4) != 0) return;
  const size_t text = dat + 8, text_end = std::min(d.size(), text + u32(dat + 4));
  // One guest block for all of them (they live as long as the game).
  const uint32_t size = uint32_t((text_end - text + 0xFFFF) & ~size_t(0xFFFF));
  const uint64_t block = g_proc->vm.Reserve(0, size);
  if (!block || !g_proc->vm.Commit(block, size, Prot::RW)) return;
  std::memcpy(GPtr(uint32_t(block)), d.data() + text, text_end - text);
  for (uint32_t i = 0; i + 8 <= keys; i += 8) {
    const uint32_t off = u32(12 + i), hash = u32(16 + i);
    if (text + off < text_end) g_texts[hash] = uint32_t(block) + off;
  }
  WarnF("gtaiv: update folder: %zu texts from %s\n", g_texts.size(), host.c_str());
}

uint32_t FindText(uint32_t hash) {
  std::lock_guard<std::mutex> l(g_text_mu);
  auto it = g_texts.find(hash);
  return it == g_texts.end() ? 0 : it->second;
}

}  // namespace

// The GXT file's path for the language (thiscall, three arguments): its
// "FF" companion in the update folder is read along (american.gxt ->
// common\text\americanFF.gxt).
void f_008A3F70_lifted(Ctx& c);
void f_008A3F70(Ctx& c) {
  f_008A3F70_lifted(c);
  if (!FfData() || !c.eax) return;
  std::string name(reinterpret_cast<const char*>(GPtr(c.eax)));
  name = name.substr(name.find_last_of("/\\:") + 1);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return;
  const std::string rel = Lower("common\\text\\" + name.substr(0, dot) + "FF" + name.substr(dot));
  const UpdateFolder& f = Folder();
  auto it = f.files.find(rel);
  std::lock_guard<std::mutex> l(g_text_mu);
  if (it == f.files.end() || it->second == g_text_file) return;
  g_text_file = it->second;
  std::string host = it->second;
  std::replace(host.begin(), host.end(), '\\', '/');
  LoadTexts(g_proc->game_dir + "/update/" + host);
}

// CText::GetTextByKey (thiscall: key hash, and one more; returns the wide
// string): FusionFix's first.
void f_008A3BF0_lifted(Ctx& c);
void f_008A3BF0(Ctx& c) {
  if (const uint32_t s = FindText(Rd32(c.esp + 4))) {
    c.eax = s;
    c.esp += 12;  // ret 8
    return;
  }
  f_008A3BF0_lifted(c);
}

// CText::DoesTextLabelExist (thiscall: key): FusionFix's count too (the
// key's hash by the game's own, 0045CF50).
void f_008A3CF0_lifted(Ctx& c);
void f_008A3CF0(Ctx& c) {
  const uint32_t key = Rd32(c.esp + 4), self = c.ecx;
  if (key && FfData()) {
    const uint32_t hash = uint32_t(CallGuest(c, 0x0045CF50, {key, 0}));
    if (FindText(hash)) {
      c.eax = 1;
      c.esp += 8;  // ret 4
      return;
    }
    c.ecx = self;
  }
  f_008A3CF0_lifted(c);
}

// The shader folder for the graphics card (mov edx, [4*eax + 00F4DB8C]:
// win32_30, win32_30_low_ati, ..._nv8): FusionFix's shaders come in
// win32_30 only, so with them that one (as FusionFix does).
void hook_00AC4DE8(Ctx& c) {
  if (FfShaders()) c.eax = 0;
  c.edx = Rd32(0x00F4DB8C + 4 * c.eax);
}

// FusionFix's new shaders (gta_trees_extended and others no model of the
// game's own uses; mods' models do, and fail with EFC20 without them): the
// game loads only those named in common\shaders\preload.list, each by
// grmShaderFxTemplate::LoadIntoSlot (00418820, cdecl: name without
// extension, slot). After the list (at 0041E550, the slot in ebp), the
// update folder's .fxc the list did not name are loaded the same way
// (FusionFix's preload list, by its idea). The shaders the game sets up
// on its own (rage_postfx, water...) are not. Without FusionFix's shaders
// the same for what the mods have of the models' (gta_...), from the game's
// folder when it has them and otherwise the mod's: a model needs its shader
// even when the rest of FusionFix's are off.
namespace {
std::mutex g_preload_mu;
std::vector<std::string> g_preloaded;  // lower case, as loaded
}  // namespace

void f_00418820_lifted(Ctx& c);
void f_00418820(Ctx& c) {
  if (const uint32_t name = Rd32(c.esp + 4)) {
    std::lock_guard<std::mutex> l(g_preload_mu);
    g_preloaded.push_back(Lower(reinterpret_cast<const char*>(GPtr(name))));
  }
  f_00418820_lifted(c);
}

void hook_0041E550(Ctx& c) {  // cmp [edi+14], ebx
  static bool done = false;
  if (FfData() && !done) {
    done = true;
    static const char* const kOwn[] = {"rage_im",        "rage_bink",       "shadowsmartblit", "shadowz",
                                       "mirror",         "water",           "rmptfx_default",  "rmptfx_litsprite",
                                       "rage_postfx",    "cascadeconvert",  "cascadegen",      "deferred_lighting",
                                       "gpuptfx_update", "gpuptfx_simplerender", "rage_perlinnoise"};
    std::vector<std::string> names;
    Folder();  // (its shaders noted)
    for (const auto& [stem, source] : g_mod_fxc) {
      if (!FfShaders() && stem.rfind("gta_", 0) != 0) continue;
      bool skip = false;
      for (const char* own : kOwn) skip = skip || stem == own;
      {
        std::lock_guard<std::mutex> l(g_preload_mu);
        for (const std::string& n : g_preloaded) skip = skip || n == stem;
      }
      if (!skip) names.push_back(stem);
    }
    std::sort(names.begin(), names.end());
    hook::KeepRegs keep(c);  // no calls here in the game's code
    for (const std::string& n : names) {
      const uint32_t room = (uint32_t(n.size()) + 1 + 15) & ~15u;
      c.esp -= room;
      std::memcpy(GPtr(c.esp), n.c_str(), n.size() + 1);
      CallGuest(c, 0x00418820, {c.esp, c.ebp});
      c.esp += room;
      WarnF("gtaiv: update folder: shader %s loaded (not in the preload list)\n", n.c_str());
    }
  }
  Flags f;
  UnpackEFlags(f, c.eflags);
  lift::Sub<4>(f, Rd32(c.edi + 0x14), c.ebx);
  c.eflags = PackEFlags(f);
}

}  // namespace recomp::gen
