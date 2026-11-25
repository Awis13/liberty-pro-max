#pragma once

#include <string>

namespace language {

inline std::string ArtworkPathKey(std::string path) {
  for (char& c : path) {
    if (c == '/') c = '\\';
    else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return path;
}

// A missing translated title texture must not change the game's language.
// Probe existing files without invoking this redirect again; overrides may
// point outside the install (for example, to a mod). Never redirect GXT or
// font files: their availability still determines usable game languages.
template <class Exists, class Override>
std::string TitleArtworkRedirect(const std::string& path, const std::string& install,
                                 Exists exists, Override override_path) {
  const std::string key = ArtworkPathKey(path);
  const std::string prefix = ArtworkPathKey(install) + "\\pc\\textures\\title_offline";
  if (key.compare(0, prefix.size(), prefix) != 0) return {};
  const std::string suffix = key.substr(prefix.size());
  const size_t locale = suffix.size() == 7 ? 1 : suffix.size() == 10 ? 4 : 0;
  if (!locale || (locale == 4 && suffix.substr(0, 4) != "_e1_" && suffix.substr(0, 4) != "_e2_") ||
      suffix[locale - 1] != '_' || suffix.substr(locale + 2) != ".wtd" ||
      suffix[locale] < 'a' || suffix[locale] > 'z' ||
      suffix[locale + 1] < 'a' || suffix[locale + 1] > 'z' ||
      suffix.substr(locale, 2) == "en" || exists(path)) return {};
  std::string english = path;
  english.replace(prefix.size() + locale, 2, "en");
  std::string target = override_path(english);
  if (target.empty()) target = english;
  return exists(target) ? target : std::string{};
}

}  // namespace language
