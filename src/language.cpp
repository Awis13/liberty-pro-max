// Russian: the game in Russian when the user's language is (main.cpp; or
// GTAIV_LANG_ID=0x0419) and the game folder has the Russian files, as
// Steam's install does. The western 1.0.8.0 exe draws Russian itself (the
// fonts_r textures and FONTS_R.DAT for the region letter 'r', the title
// screens and every table of language 5), but never chooses it:
//
//   - its language lookup (007BEF90) makes Russian American; with the
//     Russian files it answers 5 here (6, Japanese, with the text object's
//     own flag, as the game does). Every user of the language handles 5:
//     the title screens (0047A6F5, 007C808D), the other tables of seven
//     (00484925), those that check the range first, and those that test
//     for 5 alone (004A20E3, 004012EF's message box);
//   - the text file's name (008A3880) knows languages 0-4 and gives the
//     rest AMERICAN.GXT, with the region letter 'e': for 5, RUSSIAN.GXT and
//     'r' here (its case does not matter: the runtime finds files without
//     case, as TBoGT's russian.GXT);
//   - the text object's constructor (008A3E02) sets 'e': 'r' here, so that
//     the fonts the game loads before its text are already Russian;
//   - the streamed font at start (00887DB0, and its fallback path at
//     00887E01) is always the western one, where loading it again
//     (008872EF) takes fonts_r's with 'r': the same choice here.
//
// russian.gxt is older than american.gxt: a text the game asks for that
// it lacks comes from american.gxt (MissingTexts, language.h), through the
// text lookups update_folder.cpp replaces. Those are English, drawn with
// the Russian font, which has the Latin letters (Russian texts use them:
// "Xbox LIVE", "GTA IV").
//
// Without the files (IV's text, fonts and title screen, and with the
// episodes on their texts and title screens) the game stays in English,
// with one line in the log.
//
// The game's own language menu and its settings: SETTINGS.CFG keeps the
// lookup's answer and the language in use (010FC134, 010FC138), and the
// profile keeps the language as setting 0xCE when the two differ (007BD4D4).
// Read back, the file's language would override the lookup on every start
// (007C70D0 runs after 00481560), so a new system language, or the
// launcher's English, never applied once the game had run. Here the
// lookup's answer applies unless the file's language was chosen in the
// menu (it differs from the lookup's answer then); a stored language the
// game cannot use here (Russian without the files, Japanese without its
// flag, past Japanese) is the lookup's. The menu's own rule keeps Russian out of its cycle (and
// Japanese, which only its slider can give); with Russian on the cycle
// goes through Russian (language.h, MenuLanguage). Saves hold no language.
//
// One text-scale tweak (004A20DE, for the legal screens at start) asks
// the lookup and applies when it answers Russian: with Russian on it takes
// the language of the text loaded instead, by its region letter, so a
// western text gets the western scale. The lookup itself keeps answering
// Russian: 00494384 compares it with the game's language to load a menu
// choice's text at start.

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "episode_files.h"
#include "hook_util.h"
#include "language.h"
#include "option.h"
#include "runtime/hle/kernel32_state.h"
#include "runtime/hle/locale.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

void f_007BEF90_lifted(Ctx& c);
void f_008A3880_lifted(Ctx& c);

namespace {

constexpr gaddr kLookupLanguage = 0x010FC134;    // the language lookup's answer at start
constexpr gaddr kLanguage = 0x010FC138;          // the game's language (GameLanguage)
constexpr gaddr kAppliedLanguage = 0x010FC13C;   // the language in use (the menu's previous)
constexpr gaddr kRegion = 0x011FB728;            // the text object's region letter
constexpr gaddr kJapaneseFlag = 0x011FB72B;      // the text object's own Japanese
constexpr gaddr kStartingEpisode = 0x0112D788;   // 0 IV, 1 TLAD, 2 TBoGT
constexpr uint32_t kTextRegion = 0x260, kTextJapanese = 0x263;  // in the text object

// The user's language as kernel32 tells the game: GameConfig::user_lang_id,
// or GTAIV_LANG_ID (the runtime fails on one it cannot read).
uint16_t UserLangId() {
  uint16_t id = Game().user_lang_id;
  if (const char* e = std::getenv("GTAIV_LANG_ID"); e && *e) ParseLangId(e, &id);
  return id;
}

// Whether a file below the install (backslashes) is there, read as the game
// reads it: a mod's or the update folder's first, names without case.
bool HasFile(const std::string& rel) {
  struct stat st;
  const std::string host = HostPath(Game().guest_dir + "\\" + rel);
  return stat(host.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool Russian() {
  static const bool on = [] {
    const uint16_t id = UserLangId();
    if ((id & 0x3FF) != 0x19) return false;
    language::RussianFiles f;
    f.text = HasFile("common\\text\\russian.gxt");
    f.font = HasFile("pc\\textures\\fonts_r.wtd");
    f.streamed_font = HasFile("pc\\textures\\fonts_r_streamed_1.wtd");
    f.font_data = HasFile("common\\data\\fonts_r.dat");
    f.title = HasFile("pc\\textures\\title_offline_ru.wtd");
    if (OptionOn("GTAIV_EPISODES", false)) {
      f.tlad = EpisodeInstalled(episode_files::kTLAD);
      f.tlad_text = HasFile("TLAD\\common\\text\\russian.gxt");
      f.tlad_title = HasFile("pc\\textures\\title_offline_e1_ru.wtd");
      f.tbogt = EpisodeInstalled(episode_files::kTBoGT);
      f.tbogt_text = HasFile("TBoGT\\common\\text\\russian.gxt");
      f.tbogt_title = HasFile("pc\\textures\\title_offline_e2_ru.wtd");
    }
    const char* missing;
    const bool russian = language::RussianOn(id, f, &missing);
    if (russian) WarnF("gtaiv: the game in Russian\n");
    else WarnF("gtaiv: Russian: no %s in the game folder, the game stays in English\n", missing);
    return russian;
  }();
  return on;
}

// What the language lookup answers in Russian (as the game's own, 6 with
// the text object's Japanese flag).
uint32_t RussianAnswer() {
  return Rd8(kJapaneseFlag) ? language::kJapanese : language::GameLanguageFor(UserLangId(), true);
}

// What the language lookup answers (007BEF90): as the game's own, with
// Russian when Russian() is on.
uint32_t LookupAnswer() {
  return Rd8(kJapaneseFlag) ? language::kJapanese : language::GameLanguageFor(UserLangId(), Russian());
}

// Guest memory for strings the game reads (they live as long as it):
// blocks of 64 KiB, a larger string a block of its own.
uint32_t GuestCopy(const void* data, size_t size) {
  static std::mutex mu;
  static uint32_t block = 0, used = 0, end = 0;
  std::lock_guard<std::mutex> l(mu);
  const uint32_t need = uint32_t((size + 3) & ~size_t(3));
  if (!block || need > end - used) {
    const uint32_t room = std::max<uint32_t>(0x10000, (need + 0xFFFF) & ~0xFFFFu);
    const uint64_t b = g_proc->vm.Reserve(0, room);
    if (!b || !g_proc->vm.Commit(b, room, Prot::RW)) return 0;
    block = used = uint32_t(b);
    end = block + room;
  }
  const uint32_t va = used;
  std::memcpy(GPtr(va), data, size);
  used += need;
  return va;
}

// The English texts russian.gxt lacks, per game (IV, TLAD, TBoGT): key
// hash -> guest wide string. Read the first time one is asked for.
struct Missing {
  bool read = false;
  std::unordered_map<uint32_t, uint32_t> texts;
};
std::mutex g_missing_mu;
Missing g_missing[3];

std::vector<uint8_t> ReadAll(const std::string& rel) {
  std::ifstream in(HostPath(Game().guest_dir + "\\" + rel), std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void ReadMissing(uint32_t game, Missing& m) {
  m.read = true;
  const std::string dir = std::string(game == 1 ? "TLAD\\" : game == 2 ? "TBoGT\\" : "") + "common\\text\\";
  const std::vector<uint8_t> american = ReadAll(dir + "american.gxt"), russian = ReadAll(dir + "russian.gxt");
  language::GxtTables am, ru;
  if (!language::ReadGxt(american.data(), american.size(), &am) ||
      !language::ReadGxt(russian.data(), russian.size(), &ru)) {
    WarnF("gtaiv: Russian: %samerican.gxt or russian.gxt unreadable, no English for missing texts\n", dir.c_str());
    return;
  }
  for (const auto& [hash, text] : language::MissingTexts(american.data(), american.size(), am, ru)) {
    std::u16string s = text;
    s += u'\0';
    if (const uint32_t va = GuestCopy(s.data(), s.size() * sizeof(char16_t))) m.texts[hash] = va;
  }
  WarnF("gtaiv: Russian: %zu texts from %samerican.gxt that russian.gxt lacks\n", m.texts.size(), dir.c_str());
}

}  // namespace

// The English text for a key the game did not find in Russian (the game
// being played's own, then IV's), or 0.
uint32_t RussianMissingText(uint32_t hash) {
  if (!Russian()) return 0;
  const uint32_t game = Rd32(kStartingEpisode);
  std::lock_guard<std::mutex> l(g_missing_mu);
  for (const uint32_t g : {game, 0u}) {
    if (g > 2) continue;
    Missing& m = g_missing[g];
    if (!m.read) ReadMissing(g, m);
    if (auto it = m.texts.find(hash); it != m.texts.end()) return it->second;
  }
  return 0;
}

bool RussianText() { return Russian(); }

// The language lookup (cdecl, no arguments).
void f_007BEF90(Ctx& c) {
  if (!Russian()) return f_007BEF90_lifted(c);
  c.eax = RussianAnswer();
  c.esp += 4;  // ret
}

// The text file's name (thiscall: whether to ask the lookup or take the
// game's language; ret 4), and the region letter.
void f_008A3880(Ctx& c) {
  const uint32_t self = c.ecx;
  const bool ask = Rd8(c.esp + 4) != 0;
  f_008A3880_lifted(c);
  if (!Russian()) return;
  const uint32_t lang = ask ? RussianAnswer() : Rd32(kLanguage);
  if (lang != language::kRussian || Rd8(self + kTextJapanese)) return;
  static const uint32_t name = [] {
    static const char kName[] = "RUSSIAN.GXT";
    return GuestCopy(kName, sizeof kName);
  }();
  if (!name) return;
  Wr8(self + kTextRegion, 'r');
  c.eax = name;
}

// The text object's constructor: mov byte [esi+260], 'e'.
void hook_008A3E02(Ctx& c) { Wr8(c.esi + kTextRegion, Russian() ? 'r' : 'e'); }

// SETTINGS.CFG read (007C70D0), after either of its copies into the
// settings block, which holds the lookup's answer when the file was written
// and the language then: push ebp. The lookup's answer now applies unless
// the language was chosen in the game's menu. An explicit launcher choice
// takes priority over that stored choice (language::StartLanguage).
void hook_007C72E7(Ctx& c) {
  const uint32_t stored = Rd32(kLanguage), lookup = LookupAnswer();
  const uint32_t lang =
      language::StartLanguage(Rd32(kLookupLanguage), stored, lookup, Russian(), Rd8(kJapaneseFlag) != 0,
                              OptionOn("GTAIV_LAUNCHER_LANGUAGE", false));
  Wr32(kLookupLanguage, lookup);
  if (lang != stored) {
    Wr32(kLanguage, lang);
    Wr32(kAppliedLanguage, lang);
  }
  Push(c, c.ebp);
}

// The profile's language (setting 0xCE, kept by the game when it differs
// from the lookup's answer) read back: mov [010FC138], eax. One the game
// cannot use here is the lookup's (language::StoredLanguage).
void hook_007C9685(Ctx& c) {
  Wr32(kLanguage, language::StoredLanguage(c.eax, LookupAnswer(), Russian(), Rd8(kJapaneseFlag) != 0,
                                         OptionOn("GTAIV_LAUNCHER_LANGUAGE", false)));
}

// The game's language menu, after its arrows or its slider set a language
// ([010FC138] the new one, [010FC13C] the one in use): the game's rule that
// keeps Russian and Japanese out, or with Russian on every language through
// Russian (language::MenuLanguage). The block 007C7A29-007C7A5A it replaces only
// sets [010FC138] (eax is not used after it).
void hook_007C7A29(Ctx& c) {
  const uint32_t chosen = Rd32(kLanguage);
  const uint32_t lang = language::MenuLanguage(chosen, Rd32(kAppliedLanguage), Russian() && !Rd8(kJapaneseFlag));
  if (lang != chosen) Wr32(kLanguage, lang);
  c.eax = lang;
}

// The text-scale tweak's test of the lookup's answer: cmp eax, 5. Its
// function (004A2040) draws the legal screens (LEGAL_PC, LEGAL_PC_2; called
// at 004A1CA4, 004A1CCD) at start, before a language chosen in the menu is
// loaded (00494398), from the text object, whose region letter 008A3880
// sets for each text file it loads. With Russian on: Russian when that text
// is Russian ('r'), otherwise not.
void hook_004A20E3(Ctx& c) {
  const uint32_t text = Rd8(kRegion) == 'r' ? language::kRussian : language::kAmerican;
  CmpFlags<4>(c, Russian() ? text : c.eax, language::kRussian);
}

// The streamed font at start: mov dword [esp+9C], 2B (the western one's
// kind); 31, fonts_r's, with the region letter 'r' (as 008872EF does).
void hook_00887DB0(Ctx& c) { Wr32(c.esp + 0x9C, Rd8(kRegion) == 'r' ? 0x31 : 0x2B); }

// Its path when none was found that way: push "platform:/textures/
// fonts_streamed_1" (00D87D68); fonts_r_streamed_1 (00D8779C) with 'r'.
void hook_00887E01(Ctx& c) { Push(c, Rd8(kRegion) == 'r' ? 0x00D8779C : 0x00D87D68); }

}  // namespace recomp::gen
