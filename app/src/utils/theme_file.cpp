#include "utils/theme_file.hpp"

#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "utils/misc.hpp"

#ifdef USE_LIBROMFS
#include <romfs/romfs.hpp>
#endif

namespace themes {

static std::vector<ThemeFile> loaded;

static bool parseColor(const std::string& s, NVGcolor& out) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return false;
    unsigned int r = 0, g = 0, b = 0, a = 255;
    if (std::sscanf(s.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3) return false;
    if (s.size() == 9 && std::sscanf(s.c_str() + 7, "%2x", &a) != 1) return false;
    out = nvgRGBA(r, g, b, a);
    return true;
}

static void parse(const std::string& id, const std::string& data) {
    try {
        auto j = nlohmann::json::parse(data);
        ThemeFile t;
        t.id = id;
        t.name = j.value("name", id);
        t.base = j.value("base", std::string("dark")) == "light" ? brls::ThemeVariant::LIGHT : brls::ThemeVariant::DARK;
        if (j.contains("colors") && j["colors"].is_object()) {
            for (auto& [key, value] : j["colors"].items()) {
                NVGcolor color;
                if (value.is_string() && parseColor(value.get<std::string>(), color)) {
                    t.colors.emplace_back(key, color);
                } else {
                    brls::Logger::warning("theme {}: bad color for {}", id, key);
                }
            }
        }
        auto it = std::find_if(loaded.begin(), loaded.end(), [&](const ThemeFile& o) { return o.id == id; });
        if (it != loaded.end())
            *it = std::move(t);
        else
            loaded.push_back(std::move(t));
    } catch (const std::exception& e) {
        brls::Logger::error("theme {}: {}", id, e.what());
    }
}

static void loadDir(const fs::path& dir) {
    try {
        if (!fs::is_directory(dir)) return;
        std::vector<fs::path> files;
        for (auto& entry : fs::directory_iterator(dir))
            if (entry.path().extension() == ".json") files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (auto& file : files) {
            std::ifstream in(file.string());
            std::stringstream ss;
            ss << in.rdbuf();
            parse(file.stem().string(), ss.str());
        }
    } catch (const std::exception& e) {
        brls::Logger::warning("themes {}: {}", dir.string(), e.what());
    }
}

void load(const std::string& userDir) {
    loaded.clear();
#ifdef USE_LIBROMFS
    std::vector<fs::path> files;
    for (auto& path : romfs::list("themes"))
        if (path.extension() == ".json") files.push_back(path);
    std::sort(files.begin(), files.end());
    for (auto& path : files) {
        auto& res = romfs::get(path);
        parse(path.stem().string(), std::string(res.string()));
    }
#else
    loadDir(fs::path(BRLS_RESOURCES) / "themes");
#endif
    loadDir(fs::path(userDir) / "themes");
}

const std::vector<ThemeFile>& all() { return loaded; }

const ThemeFile* find(const std::string& id) {
    for (auto& t : loaded)
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace themes
