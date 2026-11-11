// language (src/language.h): the system's preferred language as the
// Windows language id the game is given.

#include "language.h"

#include <cstdio>

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

}  // namespace

int main() {
  TestLocales();
  TestPrimary();
  TestOthers();
  if (g_failures) return 1;
  std::puts("language_test: ok");
  return 0;
}
