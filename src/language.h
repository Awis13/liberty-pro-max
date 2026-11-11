// The game's language: the system's, as a Windows language id (LANGID)
// the runtime gives the game (GameConfig::user_lang_id; main.cpp). Plain
// C++: tests/language_test.cpp checks it.
//
// The game asks GetUserDefaultUILanguage and keeps the primary language
// alone (007BEF90: & 0x3FF), so the sublanguage given here only has to be
// a real one: each language's own country, as Windows sets it up there.
#pragma once

#include <cstdint>
#include <string_view>

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

}  // namespace language
