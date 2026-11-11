#include "language.h"
#include "title_artwork.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace {
int failures = 0;
#define EXPECT(value) do { if (!(value)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #value); ++failures; } } while (0)
const std::string install = "C:\\Games\\GTAIV";
std::string Title(const std::string& episode, const std::string& locale) {
  return install + "\\pc\\textures\\title_offline" + episode + "_" + locale + ".wtd";
}
struct Files {
  std::set<std::string> files;
  std::map<std::string, std::string> overrides;
  std::string Redirect(const std::string& path) const {
    return language::TitleArtworkRedirect(path, install,
        [&](const std::string& p) { return files.count(language::ArtworkPathKey(p)) != 0; },
        [&](const std::string& p) {
          auto i = overrides.find(language::ArtworkPathKey(p));
          return i == overrides.end() ? std::string{} : i->second;
        });
  }
  void Add(const std::string& path) { files.insert(language::ArtworkPathKey(path)); }
};
}

int main() {
  // Every supported title locale, in IV and both episodes. The fallback
  // selects artwork only; it does not select a new text language.
  for (const std::string episode : {"", "_e1", "_e2"}) {
    for (const std::string locale : {"fr", "de", "it", "es", "ru", "jp", "ja"}) {
      Files f; f.Add(Title(episode, "en"));
      EXPECT(f.Redirect(Title(episode, locale)) == Title(episode, "en"));
      f.Add(Title(episode, locale));
      EXPECT(f.Redirect(Title(episode, locale)).empty());
    }
  }
  Files f; f.Add(Title("_e1", "en"));
  EXPECT(f.Redirect(Title("_e1", "en")).empty());
  EXPECT(f.Redirect(Title("_e2", "ru")).empty()); // English also absent.
  EXPECT(f.Redirect("C:/GAMES/GTAIV/PC/TEXTURES/TITLE_OFFLINE_E1_RU.WTD") == "C:/GAMES/GTAIV/PC/TEXTURES/TITLE_OFFLINE_E1_en.WTD");
  for (const std::string path : {"common/text/russian.gxt", "pc/textures/fonts_r.wtd",
       "pc/textures/other_ru.wtd", "pc/textures/title_offline_extra_ru.wtd",
       "pc/textures/title_offline_e1_russian.wtd"})
    EXPECT(f.Redirect(install + "/" + path).empty());
  EXPECT(f.Redirect("C:/Other/pc/textures/title_offline_e1_ru.wtd").empty());
  // An English mod/update override is used, including when no stock
  // English texture exists. A missing override is not returned.
  Files mod;
  const std::string replacement = "C:/mods/title/title_offline_e1_en.wtd";
  mod.overrides[language::ArtworkPathKey(Title("_e1", "en"))] = replacement;
  EXPECT(mod.Redirect(Title("_e1", "ru")).empty());
  mod.Add(replacement);
  EXPECT(mod.Redirect(Title("_e1", "ru")) == replacement);
  // Reproduce the Steam DLC combination: Russian GXT/fonts exist, the
  // episode title textures do not, and English artwork is available.
  Files steam; steam.Add(Title("_e1", "en")); steam.Add(Title("_e2", "en"));
  language::RussianFiles ru;
  ru.text = ru.font = ru.streamed_font = ru.font_data = ru.title = true;
  ru.tlad = ru.tlad_text = ru.tbogt = ru.tbogt_text = true;
  const char* missing = nullptr;
  EXPECT(!language::RussianOn(0x0419, ru, &missing));
  ru.tlad_title = !steam.Redirect(Title("_e1", "ru")).empty();
  ru.tbogt_title = !steam.Redirect(Title("_e2", "ru")).empty();
  EXPECT(language::RussianOn(0x0419, ru, &missing) && missing == nullptr);
  ru.font = false;
  EXPECT(!language::RussianOn(0x0419, ru, &missing));
  if (failures) return 1;
  std::puts("title_artwork_test: ok");
}
