#!/usr/bin/env python3
"""Exercise saved appearance updates and the pinned mpv ASS style boundary."""
from source_fixture import native_source
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from source_fixture import function

ROOT = Path(__file__).resolve().parents[2]


def compile_run(source, cxx=True):
    with tempfile.TemporaryDirectory() as temporary:
        code = Path(temporary) / ("appearance.cpp" if cxx else "appearance.c")
        binary = Path(temporary) / "appearance"
        code.write_text(source)
        subprocess.run([
            os.environ.get("CXX" if cxx else "CC", "clang++" if cxx else "clang"),
            "-std=c++17" if cxx else "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-I", str(ROOT / "app/include"), str(code), "-lm", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


class SubtitleAppearanceTests(unittest.TestCase):
    def test_actual_adapter_and_preference_lifetime(self):
        adapter = function(native_source(ROOT / "app/src/view/mpv_core.cpp"), "applySubtitlePreferences")
        fixture = r'''
#include "utils/subtitle_appearance.hpp"
#include <cassert>
#include <map>
#include <string>

struct AppConfig {
    enum Item { PS5_SUBTITLE_SIZE, PS5_SUBTITLE_MARGIN };
    std::map<Item, int> settings;
    static AppConfig& instance() { static AppConfig conf; return conf; }
    int getItem(Item key, int fallback) const {
        auto found = settings.find(key);
        return found == settings.end() ? fallback : found->second;
    }
};
struct MPVCore {
    // Deliberately non-default user mpv.conf values.
    subtitle_appearance::Preferences subtitlePreferences{40, 17};
    std::map<std::string, double> writes;
    void setDouble(const std::string& key, double value) { writes[key] = value; }
    void setInt(const std::string& key, int64_t value) { writes[key] = value; }
    void applySubtitlePreferences();
};
@ADAPTER@
int main() {
    auto& settings = AppConfig::instance().settings;
    MPVCore player;
    player.applySubtitlePreferences();
    assert(player.writes.empty()); // No saved UI choices leave mpv.conf alone.
    settings[AppConfig::PS5_SUBTITLE_SIZE] = 75;
    player.applySubtitlePreferences();
    assert(player.writes.size() == 1 && player.writes.at("sub-font-size") == 30);
    settings[AppConfig::PS5_SUBTITLE_MARGIN] = 60;
    player.applySubtitlePreferences();
    assert(player.writes.size() == 2 && player.writes.at("sub-margin-y") == 60);
    // Update both controls repeatedly: font scaling never compounds.
    for (int i = 0; i < 100; ++i) player.applySubtitlePreferences();
    assert(player.writes.at("sub-font-size") == 30);
    assert(player.writes.at("sub-margin-y") == 60);
    // A new player instance applies the same saved settings to its own config.
    MPVCore reopened;
    reopened.subtitlePreferences = {60, 9};
    reopened.applySubtitlePreferences();
    assert(reopened.writes.at("sub-font-size") == 45);
    assert(reopened.writes.at("sub-margin-y") == 60);
    // Reset only size; preserve the selected position.
    settings[AppConfig::PS5_SUBTITLE_SIZE] = 0;
    player.writes.clear();
    player.applySubtitlePreferences();
    assert(player.writes.at("sub-font-size") == 40);
    assert(player.writes.at("sub-margin-y") == 60);
    settings[AppConfig::PS5_SUBTITLE_MARGIN] = -1;
    player.writes.clear();
    player.applySubtitlePreferences();
    assert(player.writes.size() == 1 && player.writes.at("sub-margin-y") == 17);
    player.writes.clear();
    player.applySubtitlePreferences();
    assert(player.writes.empty());
    // Damaged/unsupported preferences restore user defaults, then stop writing.
    settings[AppConfig::PS5_SUBTITLE_SIZE] = 125;
    settings[AppConfig::PS5_SUBTITLE_MARGIN] = 90;
    player.applySubtitlePreferences();
    settings[AppConfig::PS5_SUBTITLE_SIZE] = -100;
    settings[AppConfig::PS5_SUBTITLE_MARGIN] = 100000;
    player.writes.clear();
    player.applySubtitlePreferences();
    assert(player.writes.at("sub-font-size") == 40);
    assert(player.writes.at("sub-margin-y") == 17);
    player.writes.clear();
    player.applySubtitlePreferences();
    assert(player.writes.empty());
    for (size_t i = 0; i < subtitle_appearance::sizes.size(); ++i)
        assert(subtitle_appearance::indexOf(subtitle_appearance::sizes,
            subtitle_appearance::sizes[i]) == static_cast<int>(i));
    for (size_t i = 0; i < subtitle_appearance::margins.size(); ++i)
        assert(subtitle_appearance::indexOf(subtitle_appearance::margins,
            subtitle_appearance::margins[i]) == static_cast<int>(i));
    assert(subtitle_appearance::indexOf(subtitle_appearance::sizes, 2147483647) == 0);
}
'''
        compile_run(fixture.replace("@ADAPTER@", adapter))


if __name__ == '__main__': unittest.main()
