#pragma once

#include <borealis/core/theme.hpp>
#include <string>
#include <utility>
#include <vector>

/// A theme file builds on the light or dark theme and overrides some of its
/// colors. Bundled themes live in resources/themes/, user themes in the
/// themes/ folder of the config directory. Format:
///
///   {
///     "name": "Black",
///     "base": "dark",
///     "colors": { "brls/background": "#000000", "color/app": "#00a4dc" }
///   }
///
/// Colors are "#RRGGBB" or "#RRGGBBAA". Keys are the borealis ("brls/...")
/// and app ("color/...", "font/...") theme color names.
struct ThemeFile {
    std::string id;    // file name without .json, stored in app_theme
    std::string name;  // shown in settings
    brls::ThemeVariant base = brls::ThemeVariant::DARK;
    std::vector<std::pair<std::string, NVGcolor>> colors;
};

namespace themes {

/// Load bundled themes, then user themes from `userDir` (same id replaces).
void load(const std::string& userDir);

const std::vector<ThemeFile>& all();

/// nullptr when `id` is not a loaded theme file
const ThemeFile* find(const std::string& id);

}  // namespace themes
