// The game's language: the system's, as a Windows language id (LANGID)
// the runtime gives the game (GameConfig::user_lang_id; main.cpp), and
// Russian (language.cpp). Plain C++: tests/language_test.cpp checks it.
//
// The game asks GetUserDefaultUILanguage and keeps the primary language
// alone (007BEF90: & 0x3FF), so the sublanguage given here only has to be
// a real one: each language's own country, as Windows sets it up there.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace language {

constexpr uint16_t kEnglish = 0x0409;  // US English: the game's American text

// A language from the system's list of preferred languages ("ru", "ru-RU",
// "en_GB", "es-419", "zh-Hans-CN"): its first part, without case, as the
// language id of a language the game has text for; any other, English.
inline uint16_t LangIdForLocale(std::string_view code) {
  const size_t end = code.find_first_of("-_.@");
  const std::string_view lang = code.substr(0, end);
  if (lang.size() != 2) return kEnglish;
  const char a = char(lang[0] | 0x20), b = char(lang[1] | 0x20);
  struct Entry {
    char a, b;
    uint16_t id;
  };
  static constexpr Entry kLanguages[] = {
      {'e', 'n', 0x0409},  // English (United States)
      {'f', 'r', 0x040C},  // French (France)
      {'d', 'e', 0x0407},  // German (Germany)
      {'i', 't', 0x0410},  // Italian (Italy)
      {'e', 's', 0x0C0A},  // Spanish (Spain, modern sort: Windows' own for Spain)
      {'r', 'u', 0x0419},  // Russian (Russia)
  };
  for (const Entry& e : kLanguages)
    if (e.a == a && e.b == b) return e.id;
  return kEnglish;
}

// The game's language numbers: the order of its text files and title
// screens, [010FC138].
enum GameLanguage : uint32_t {
  kAmerican = 0,
  kFrench = 1,
  kGerman = 2,
  kItalian = 3,
  kSpanish = 4,
  kRussian = 5,
  kJapanese = 6,
};

// The game's language for a language id: its own lookup (007BEF90, the
// table at 007BEFFC) by the primary language, which makes Russian (and
// every language it has no text for) American; with the Russian files
// (russian), Russian. Japanese the lookup gives only with the text
// object's own flag (011FB72B), never by the language id.
inline uint32_t GameLanguageFor(uint16_t lang_id, bool russian) {
  switch (lang_id & 0x3FF) {
    case 0x07: return kGerman;
    case 0x0A: return kSpanish;
    case 0x0C: return kFrench;
    case 0x10: return kItalian;
    case 0x19: return russian ? kRussian : kAmerican;
    default: return kAmerican;
  }
}

// The game's language menu (007C7A29): `chosen` the language it was set
// to ([010FC138]), `applied` the one in use ([010FC13C]). Its arrows
// (007C9D46-007C9E08, display kind 5) step over 0-5 and wrap (past 5 to 0,
// below 0 to 5; with the text object's Japanese flag only 0, and the game
// then sets 6); its mouse slider (007CA15C-007CA1A0) sets
// round((count - 1) * x) of the menu row's count and can give 6. The
// game's own rule then keeps Russian and Japanese out: Russian reached from
// Spanish becomes American, from American Spanish; Japanese with American
// in use Spanish. With Russian on (and the Japanese flag off) every value
// the arrows give stays, so the cycle is 0-5 both ways, and the slider's 6,
// its far right, is the last language, Russian.
inline uint32_t MenuLanguage(uint32_t chosen, uint32_t applied, bool russian) {
  if (russian) return chosen == kJapanese ? kRussian : chosen;
  if (chosen == kRussian) {
    if (applied == kSpanish) return kAmerican;
    if (applied == kAmerican) return kSpanish;
  } else if (chosen == kJapanese && applied == kAmerican) {
    return kSpanish;
  }
  return chosen;
}

// A language the game stored and reads back (SETTINGS.CFG, the profile's
// setting 0xCE), with `lookup` the language lookup's answer now and
// `japanese` the text object's Japanese flag: one the game cannot use here
// is the lookup's. That is Russian without the Russian files, Japanese
// without the flag (its tables with American text and western fonts), and
// anything past Japanese. An explicit launcher choice uses the validated
// lookup answer instead of the stored setting.
inline uint32_t StoredLanguage(uint32_t stored, uint32_t lookup, bool russian, bool japanese,
                               bool launcher_choice = false) {
  if (launcher_choice) return lookup;
  if (stored > kJapanese || (stored == kRussian && !russian) || (stored == kJapanese && !japanese)) return lookup;
  return stored;
}

// The language after SETTINGS.CFG is read (007C72E7): the file keeps the
// lookup's answer when it was written ([010FC134]) and the language then
// ([010FC138]). The same: no choice was made in the game's menu, and the
// lookup's answer now applies (the system's language, or the launcher's
// language, may have changed since). Different: the menu's choice stays,
// unless the player explicitly selected a language in the launcher.
inline uint32_t StartLanguage(uint32_t stored_lookup, uint32_t stored, uint32_t lookup, bool russian, bool japanese,
                              bool launcher_choice = false) {
  if (launcher_choice || stored == stored_lookup) return lookup;
  return StoredLanguage(stored, lookup, russian, japanese);
}

// The files the game needs in Russian, found (through the mods and the
// update folder, as the game reads them) or not: IV's text, fonts (the
// textures and fonts_r.dat, its letters' widths) and title screen, and with
// the episodes on, for each episode whose folder is there, its text and its
// title screen (in IV's folder).
struct RussianFiles {
  bool text = false;           // common/text/russian.gxt
  bool font = false;           // pc/textures/fonts_r.wtd
  bool streamed_font = false;  // pc/textures/fonts_r_streamed_1.wtd
  bool font_data = false;      // common/data/fonts_r.dat
  bool title = false;          // pc/textures/title_offline_ru.wtd
  bool tlad = false;           // TLAD/common/text/american.gxt (the episode is there)
  bool tlad_text = false;      // TLAD/common/text/russian.gxt
  bool tlad_title = false;     // pc/textures/title_offline_e1_ru.wtd
  bool tbogt = false;          // TBoGT/common/text/american.gxt
  bool tbogt_text = false;     // TBoGT/common/text/russian.gxt
  bool tbogt_title = false;    // pc/textures/title_offline_e2_ru.wtd
};

// Whether the game goes Russian: a Russian language id and every file it
// needs. Otherwise *missing is the first file not found (for the log; the
// game then stays in English), or nullptr for a language id of another
// language.
inline bool RussianOn(uint16_t lang_id, const RussianFiles& f, const char** missing) {
  *missing = nullptr;
  if ((lang_id & 0x3FF) != 0x19) return false;
  if (!f.text) *missing = "common/text/russian.gxt";
  else if (!f.font) *missing = "pc/textures/fonts_r.wtd";
  else if (!f.streamed_font) *missing = "pc/textures/fonts_r_streamed_1.wtd";
  else if (!f.font_data) *missing = "common/data/fonts_r.dat";
  else if (!f.title) *missing = "pc/textures/title_offline_ru.wtd";
  else if (f.tlad && !f.tlad_text) *missing = "TLAD/common/text/russian.gxt";
  else if (f.tlad && !f.tlad_title) *missing = "pc/textures/title_offline_e1_ru.wtd";
  else if (f.tbogt && !f.tbogt_text) *missing = "TBoGT/common/text/russian.gxt";
  else if (f.tbogt && !f.tbogt_title) *missing = "pc/textures/title_offline_e2_ru.wtd";
  return !*missing;
}

// A character of English text drawn with the Russian font, whose
// characters 0x80-0xFF are Cyrillic: a Latin-1 letter (0xC0-0xFF) becomes
// its plain letter; the rest stays.
inline char16_t PlainLatin(char16_t ch) {
  static constexpr char kPlain[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
  return ch >= 0xC0 && ch <= 0xFF ? char16_t(kPlain[ch - 0xC0]) : ch;
}

// A GXT text file of tables (version 4, 16-bit characters): "TABL", then
// its tables (an 8-character name and where it starts); each a name
// (but MAIN), "TKEY" with (string offset, key hash) pairs, "TDAT" with the
// strings. Each table (its name in capitals) as key hash -> where the
// string starts in the file; false if it is not such a file.
using GxtTable = std::unordered_map<uint32_t, uint32_t>;
using GxtTables = std::unordered_map<std::string, GxtTable>;

inline bool ReadGxt(const uint8_t* d, size_t n, GxtTables* out) {
  auto u32 = [&](size_t at, uint32_t* v) {
    if (at > n || n - at < 4) return false;
    std::memcpy(v, d + at, 4);
    return true;
  };
  uint32_t version, size;
  if (!u32(0, &version) || version != 0x00100004 || n < 12 || std::memcmp(d + 4, "TABL", 4) != 0 ||
      !u32(8, &size) || size > n - 12)
    return false;
  for (size_t e = 12; e + 12 <= 12 + size_t(size); e += 12) {
    std::string name(reinterpret_cast<const char*>(d + e), strnlen(reinterpret_cast<const char*>(d + e), 8));
    for (char& ch : name)
      if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    uint32_t at;
    if (!u32(e + 8, &at)) return false;
    if (size_t(at) + 4 <= n && std::memcmp(d + at, "TKEY", 4) != 0) at += 8;  // the table's name first
    uint32_t keys, dat_size;
    if (at > n || n - at < 8 || std::memcmp(d + at, "TKEY", 4) != 0 || !u32(at + 4, &keys) || keys > n - at - 8)
      return false;
    const size_t dat = size_t(at) + 8 + keys;
    if (n - dat < 8 || std::memcmp(d + dat, "TDAT", 4) != 0 || !u32(dat + 4, &dat_size) || dat_size > n - dat - 8)
      return false;
    GxtTable& t = (*out)[name];
    for (size_t k = size_t(at) + 8; k + 8 <= size_t(at) + 8 + keys; k += 8) {
      uint32_t off, hash;
      u32(k, &off);
      u32(k + 4, &hash);
      if (off < dat_size) t[hash] = uint32_t(dat + 8 + off);
    }
  }
  return true;
}

// The string at `at` of a GXT file: up to its 0 or the file's end.
inline std::u16string GxtString(const uint8_t* d, size_t n, uint32_t at) {
  std::u16string s;
  for (size_t c = at; c + 2 <= n; c += 2) {
    const char16_t ch = char16_t(d[c] | d[c + 1] << 8);
    if (!ch) break;
    s += ch;
  }
  return s;
}

// The English texts the game may ask for in Russian and not find: a key of
// a table of american.gxt (its bytes and tables) that russian.gxt has
// neither in the same table nor in MAIN, which is always loaded. Tables
// are matched without case (russian.gxt names its mission tables in lower
// case). The characters made plain for the Russian font (PlainLatin).
inline std::unordered_map<uint32_t, std::u16string> MissingTexts(const uint8_t* american_file, size_t n,
                                                                 const GxtTables& american,
                                                                 const GxtTables& russian) {
  std::unordered_map<uint32_t, std::u16string> out;
  static const GxtTable kNone;
  const auto main = russian.find("MAIN");
  const GxtTable& ru_main = main == russian.end() ? kNone : main->second;
  for (const auto& [name, table] : american) {
    const auto same = russian.find(name);
    const GxtTable& ru = same == russian.end() ? kNone : same->second;
    for (const auto& [hash, at] : table) {
      if (ru.count(hash) || ru_main.count(hash) || out.count(hash)) continue;
      std::u16string text = GxtString(american_file, n, at);
      for (char16_t& ch : text) ch = PlainLatin(ch);
      out.emplace(hash, std::move(text));
    }
  }
  return out;
}

}  // namespace language
