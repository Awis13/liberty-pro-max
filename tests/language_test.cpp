// language (src/language.h): the system's preferred language as the
// Windows language id the game is given; the game's language for it, with
// and without the Russian files; the English texts russian.gxt lacks.

#include "language.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "language_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

using language::LangIdForLocale;

// Each language the game has text for, by its language alone, with any
// region or script after it, written either way.
void TestLocales() {
  EXPECT(LangIdForLocale("ru") == 0x0419);
  EXPECT(LangIdForLocale("ru-RU") == 0x0419);
  EXPECT(LangIdForLocale("ru_UA") == 0x0419);
  EXPECT(LangIdForLocale("RU") == 0x0419);
  EXPECT(LangIdForLocale("en") == 0x0409);
  EXPECT(LangIdForLocale("en-GB") == 0x0409);
  EXPECT(LangIdForLocale("fr") == 0x040C);
  EXPECT(LangIdForLocale("fr-CA") == 0x040C);
  EXPECT(LangIdForLocale("de") == 0x0407);
  EXPECT(LangIdForLocale("de-AT") == 0x0407);
  EXPECT(LangIdForLocale("it") == 0x0410);
  EXPECT(LangIdForLocale("es") == 0x0C0A);
  EXPECT(LangIdForLocale("es-419") == 0x0C0A);
  EXPECT(LangIdForLocale("es_ES.UTF-8") == 0x0C0A);
}

// The game keeps the primary language (& 0x3FF): the one each id names.
void TestPrimary() {
  EXPECT((LangIdForLocale("de") & 0x3FF) == 0x07);
  EXPECT((LangIdForLocale("en") & 0x3FF) == 0x09);
  EXPECT((LangIdForLocale("es") & 0x3FF) == 0x0A);
  EXPECT((LangIdForLocale("fr") & 0x3FF) == 0x0C);
  EXPECT((LangIdForLocale("it") & 0x3FF) == 0x10);
  EXPECT((LangIdForLocale("ru") & 0x3FF) == 0x19);
  // Each with a sublanguage: never a neutral id (the runtime refuses one).
  for (const char* code : {"en", "fr", "de", "it", "es", "ru"}) EXPECT((LangIdForLocale(code) >> 10) != 0);
}

// Languages the game has no text for, and anything else: English.
void TestOthers() {
  EXPECT(LangIdForLocale("ja") == 0x0409);
  EXPECT(LangIdForLocale("ja-JP") == 0x0409);
  EXPECT(LangIdForLocale("zh-Hans") == 0x0409);
  EXPECT(LangIdForLocale("zh-Hans-CN") == 0x0409);
  EXPECT(LangIdForLocale("pt-BR") == 0x0409);
  EXPECT(LangIdForLocale("uk-UA") == 0x0409);
  EXPECT(LangIdForLocale("") == 0x0409);
  EXPECT(LangIdForLocale("-") == 0x0409);
  EXPECT(LangIdForLocale("r") == 0x0409);
  EXPECT(LangIdForLocale("rus") == 0x0409);  // three letters: not the code the system gives for it
  EXPECT(LangIdForLocale("garbage") == 0x0409);
  EXPECT(LangIdForLocale("12") == 0x0409);
  EXPECT(LangIdForLocale("r\xC3\xBC") == 0x0409);
}

// The game's own lookup (007BEFFC), and Russian only with its files.
void TestGameLanguage() {
  using language::GameLanguageFor;
  EXPECT(GameLanguageFor(0x0409, false) == 0);
  EXPECT(GameLanguageFor(0x0809, false) == 0);  // English (UK): the primary language alone
  EXPECT(GameLanguageFor(0x040C, false) == 1);
  EXPECT(GameLanguageFor(0x0407, false) == 2);
  EXPECT(GameLanguageFor(0x0410, false) == 3);
  EXPECT(GameLanguageFor(0x0C0A, false) == 4);
  EXPECT(GameLanguageFor(0x040A, false) == 4);
  EXPECT(GameLanguageFor(0x0419, false) == 0);  // as the game: American
  EXPECT(GameLanguageFor(0x0419, true) == 5);
  EXPECT(GameLanguageFor(0x0819, true) == 5);   // Russian (Moldova)
  EXPECT(GameLanguageFor(0x0411, true) == 0);   // Japanese: never by the language id
  EXPECT(GameLanguageFor(0x0804, true) == 0);
  // The Russian files change Russian alone.
  for (uint16_t id : {0x0409, 0x040C, 0x0407, 0x0410, 0x0C0A, 0x0411})
    EXPECT(GameLanguageFor(id, true) == GameLanguageFor(id, false));
  // From the system's language to the game's, Russian files there.
  EXPECT(GameLanguageFor(LangIdForLocale("ru-RU"), true) == language::kRussian);
  EXPECT(GameLanguageFor(LangIdForLocale("fr-CA"), true) == language::kFrench);
  EXPECT(GameLanguageFor(LangIdForLocale("es-419"), true) == language::kSpanish);
  EXPECT(GameLanguageFor(LangIdForLocale("ja"), true) == language::kAmerican);
}

// Russian only with every file it needs: otherwise English and the first
// file missing named.
void TestRussianFiles() {
  using language::RussianFiles;
  using language::RussianOn;
  const char* missing = "unset";
  auto lacks = [&missing](const RussianFiles& f, const char* file) {
    return !RussianOn(0x0419, f, &missing) && missing && std::string(missing) == file;
  };
  RussianFiles all;
  all.text = all.font = all.streamed_font = all.font_data = all.title = true;
  EXPECT(RussianOn(0x0419, all, &missing) && missing == nullptr);
  // Another language: never, nothing missing.
  EXPECT(!RussianOn(0x0409, all, &missing) && missing == nullptr);
  EXPECT(!RussianOn(0x040C, RussianFiles{}, &missing) && missing == nullptr);
  // Each of IV's files.
  RussianFiles f = all;
  f.text = false;
  EXPECT(lacks(f, "common/text/russian.gxt"));
  f = all;
  f.font = false;
  EXPECT(lacks(f, "pc/textures/fonts_r.wtd"));
  f = all;
  f.streamed_font = false;
  EXPECT(lacks(f, "pc/textures/fonts_r_streamed_1.wtd"));
  f = all;
  f.font_data = false;
  EXPECT(lacks(f, "common/data/fonts_r.dat"));
  f = all;
  f.title = false;
  EXPECT(lacks(f, "pc/textures/title_offline_ru.wtd"));
  EXPECT(lacks(RussianFiles{}, "common/text/russian.gxt"));
  // An episode's folder there (with the episodes on): its text and title
  // screen too; without the folder, neither.
  f = all;
  f.tlad = true;
  EXPECT(lacks(f, "TLAD/common/text/russian.gxt"));
  f.tlad_text = true;
  EXPECT(lacks(f, "pc/textures/title_offline_e1_ru.wtd"));
  f.tlad_title = true;
  EXPECT(RussianOn(0x0419, f, &missing));
  f.tbogt = true;
  EXPECT(lacks(f, "TBoGT/common/text/russian.gxt"));
  f.tbogt_text = true;
  EXPECT(lacks(f, "pc/textures/title_offline_e2_ru.wtd"));
  f.tbogt_title = true;
  EXPECT(RussianOn(0x0419, f, &missing));
  f = all;  // no episode folders: none of theirs needed
  EXPECT(RussianOn(0x0419, f, &missing));
}

// The game's language menu: its arrows step over 0-5 and wrap (007C9D46;
// with the Japanese flag only 0, not modelled: the rule is the game's
// then), its slider can also give 6 (007CA15C); then the rule.
void TestMenu() {
  using language::MenuLanguage;
  auto right = [](uint32_t from, bool russian) { return MenuLanguage(from == 5 ? 0 : from + 1, from, russian); };
  auto left = [](uint32_t from, bool russian) { return MenuLanguage(from == 0 ? 5 : from - 1, from, russian); };
  // Russian off: the game's own rule, 0-4 both ways (Russian left out).
  for (uint32_t from = 0; from < 4; from++) EXPECT(right(from, false) == from + 1);
  EXPECT(right(4, false) == 0);
  for (uint32_t from = 1; from <= 4; from++) EXPECT(left(from, false) == from - 1);
  EXPECT(left(0, false) == 4);
  // Russian on: 0-5 both ways.
  for (uint32_t from = 0; from < 5; from++) EXPECT(right(from, true) == from + 1);
  EXPECT(right(5, true) == 0);
  for (uint32_t from = 1; from <= 5; from++) EXPECT(left(from, true) == from - 1);
  EXPECT(left(0, true) == 5);
  // The full cycle comes back where it started: six languages with
  // Russian on, five without.
  for (bool russian : {true, false}) {
    for (bool forward : {true, false}) {
      uint32_t at = 0;
      int steps = 0;
      do {
        at = forward ? right(at, russian) : left(at, russian);
        EXPECT(at <= (russian ? 5u : 4u));
        steps++;
      } while (at != 0 && steps < 10);
      EXPECT(steps == (russian ? 6 : 5));
    }
  }
  // The slider: every value, its far right (6) Russian with Russian on, as
  // the game's rule has it without (Spanish with American in use, else
  // left as it is).
  for (uint32_t v = 0; v <= 5; v++)
    for (uint32_t applied = 0; applied <= 5; applied++) EXPECT(MenuLanguage(v, applied, true) == v);
  for (uint32_t applied = 0; applied <= 5; applied++) EXPECT(MenuLanguage(6, applied, true) == 5);
  EXPECT(MenuLanguage(6, 0, false) == 4);
}

// A stored language read back, and the language after SETTINGS.CFG.
void TestStored() {
  using language::StartLanguage;
  using language::StoredLanguage;
  for (uint32_t v = 0; v <= 4; v++)
    EXPECT(StoredLanguage(v, 0, false, false) == v && StoredLanguage(v, 5, true, false) == v);
  EXPECT(StoredLanguage(5, 5, true, false) == 5);
  EXPECT(StoredLanguage(5, 0, false, false) == 0);  // Russian without the files
  EXPECT(StoredLanguage(6, 0, false, true) == 6);   // Japanese with its flag
  EXPECT(StoredLanguage(6, 0, false, false) == 0);  // without: the lookup's
  EXPECT(StoredLanguage(6, 5, true, false) == 5);
  EXPECT(StoredLanguage(7, 2, false, false) == 2);  // past Japanese: the lookup's
  EXPECT(StoredLanguage(0xFFFFFFFFu, 5, true, false) == 5);
  // No choice in the menu (the file's language was its lookup's answer):
  // the lookup's answer now, the system's language or the launcher's
  // English, as it is now.
  EXPECT(StartLanguage(0, 0, 0, false, false) == 0);  // English all along: as before
  EXPECT(StartLanguage(0, 0, 5, true, false) == 5);   // an English file, now Russian
  EXPECT(StartLanguage(0, 0, 1, false, false) == 1);  // an English file, now French
  EXPECT(StartLanguage(5, 5, 0, false, false) == 0);  // Russian, then the launcher's English
  // A choice in the menu stays, over the system's and the launcher's.
  EXPECT(StartLanguage(5, 1, 5, true, false) == 1);
  EXPECT(StartLanguage(5, 1, 0, false, false) == 1);
  EXPECT(StartLanguage(0, 2, 5, true, false) == 2);
  // A choice the game cannot use here: the lookup's.
  EXPECT(StartLanguage(0, 5, 0, false, false) == 0);
  EXPECT(StartLanguage(5, 6, 5, true, false) == 5);  // Japanese without the flag
  EXPECT(StartLanguage(0, 9, 1, false, false) == 1);
  EXPECT(StartLanguage(0, 5, 5, true, false) == 5);
  // An explicit launcher choice wins over both saved settings and the
  // profile. Automatic mode above continues to preserve menu choices.
  EXPECT(StartLanguage(0, 1, 5, true, false, true) == language::kRussian);
  EXPECT(StoredLanguage(1, 5, true, false, true) == language::kRussian);
  EXPECT(StartLanguage(5, 1, 0, false, false, true) == language::kAmerican);
  EXPECT(StoredLanguage(5, 2, false, false, true) == language::kGerman);
  // Lookup has already checked the assets: requesting Russian without
  // its files must still fall back safely to English.
  EXPECT(StoredLanguage(1, language::GameLanguageFor(0x0419, false), false, false, true) == language::kAmerican);
}

// English drawn with the Russian font: Latin-1 letters made plain (its
// 0x80-0xFF are Cyrillic), the rest as it is.
void TestPlainLatin() {
  using language::PlainLatin;
  EXPECT(PlainLatin(u'A') == u'A');
  EXPECT(PlainLatin(u'~') == u'~');
  EXPECT(PlainLatin(char16_t(0xE9)) == u'e');  // e acute
  EXPECT(PlainLatin(char16_t(0xC0)) == u'A');
  EXPECT(PlainLatin(char16_t(0xD1)) == u'N');
  EXPECT(PlainLatin(char16_t(0xFC)) == u'u');
  EXPECT(PlainLatin(char16_t(0xFF)) == u'y');
  EXPECT(PlainLatin(char16_t(0xA9)) == char16_t(0xA9));  // not a letter
  EXPECT(PlainLatin(char16_t(0x0418)) == char16_t(0x0418));  // a Cyrillic letter
}

// A GXT file as the game's: TABL, then each table (MAIN first and without
// its name), its TKEY and TDAT.
using Table = std::pair<std::string, std::vector<std::pair<uint32_t, std::u16string>>>;
std::vector<uint8_t> Gxt(const std::vector<Table>& tables) {
  std::vector<uint8_t> d;
  auto u32 = [&d](uint32_t v) {
    for (int i = 0; i < 4; i++) d.push_back(uint8_t(v >> (8 * i)));
  };
  auto tag = [&d](const char* t) { d.insert(d.end(), t, t + 4); };
  u32(0x00100004);
  tag("TABL");
  u32(uint32_t(12 * tables.size()));
  const size_t index = d.size();
  d.resize(d.size() + 12 * tables.size());
  for (size_t t = 0; t < tables.size(); t++) {
    const auto& [name, entries] = tables[t];
    const uint32_t at = uint32_t(d.size());
    std::memset(&d[index + 12 * t], 0, 8);
    std::memcpy(&d[index + 12 * t], name.data(), name.size());
    std::memcpy(&d[index + 12 * t + 8], &at, 4);
    if (t) {
      d.insert(d.end(), name.begin(), name.end());
      d.resize(d.size() + 8 - name.size());
    }
    tag("TKEY");
    u32(uint32_t(8 * entries.size()));
    std::vector<uint8_t> text;
    for (const auto& [hash, s] : entries) {
      u32(uint32_t(text.size()));
      u32(hash);
      for (char16_t ch : s) {
        text.push_back(uint8_t(ch));
        text.push_back(uint8_t(ch >> 8));
      }
      text.push_back(0);
      text.push_back(0);
    }
    tag("TDAT");
    u32(uint32_t(text.size()));
    d.insert(d.end(), text.begin(), text.end());
  }
  return d;
}

void TestGxt() {
  // Russian: Cyrillic written as the game's russian.gxt does (0x80-0xFF),
  // the mission table named in lower case, one key moved to MAIN.
  const std::u16string play = {char16_t(0x96), char16_t(0x91), char16_t(0x9E), char16_t(0x8E)};
  const auto american = Gxt({
      {"MAIN", {{1, u"PLAY"}, {2, u"Activating DLC."}, {3, u"ZiT! Communiqu\u00E9"}}},
      {"B1AUD", {{10, u"~z~Great."}, {11, u"~z~The kids?"}, {12, u"Moved"}}},
      {"CSTEAL", {{20, u"The target car has been destroyed."}}},
  });
  const auto russian = Gxt({
      {"MAIN", {{1, play}, {12, u"x"}}},
      {"b1aud", {{10, u"y"}}},
      {"csteal", {}},
  });
  language::GxtTables am, ru;
  EXPECT(language::ReadGxt(american.data(), american.size(), &am));
  EXPECT(language::ReadGxt(russian.data(), russian.size(), &ru));
  EXPECT(am.size() == 3 && ru.size() == 3 && ru.count("B1AUD") == 1);
  EXPECT(am.count("MAIN") && am["MAIN"].count(2) &&
         language::GxtString(american.data(), american.size(), am["MAIN"][2]) == u"Activating DLC.");
  EXPECT(ru.count("MAIN") && ru["MAIN"].count(1) &&
         language::GxtString(russian.data(), russian.size(), ru["MAIN"][1]) == play);
  const auto missing = language::MissingTexts(american.data(), american.size(), am, ru);
  // Lacking from Russian: 2, 3 (MAIN), 11 (its mission table), 20; not 1
  // and 10 (there), nor 12 (in Russian's MAIN, always loaded).
  EXPECT(missing.size() == 4);
  EXPECT(missing.count(2) && missing.at(2) == u"Activating DLC.");
  EXPECT(missing.count(3) && missing.at(3) == u"ZiT! Communique");
  EXPECT(missing.count(11) && missing.at(11) == u"~z~The kids?");
  EXPECT(missing.count(20));
  EXPECT(!missing.count(1) && !missing.count(10) && !missing.count(12));
  // Not GXT files of tables.
  language::GxtTables none;
  const uint8_t junk[] = {1, 2, 3};
  EXPECT(!language::ReadGxt(junk, sizeof junk, &none));
  auto cut = american;
  cut.resize(cut.size() / 2);
  EXPECT(!language::ReadGxt(cut.data(), cut.size(), &none));
}

}  // namespace

int main() {
  TestLocales();
  TestPrimary();
  TestOthers();
  TestGameLanguage();
  TestRussianFiles();
  TestMenu();
  TestStored();
  TestPlainLatin();
  TestGxt();
  if (g_failures) return 1;
  std::puts("language_test: ok");
  return 0;
}
