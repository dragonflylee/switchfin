#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#endif

#include <fstream>
#include <filesystem>
#include <set>
#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include "utils/config.hpp"

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;

std::unordered_map<AppConfig::Item, AppConfig::Option> AppConfig::settingMap = {
    {APP_THEME, {"app_theme", {"auto", "light", "dark"}}},
    {APP_LANG, {"app_lang", {brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_ZH_HANS, brls::LOCALE_ZH_HANT}}},
    {KEYMAP, {"keymap", {"xbox", "ps", "keyboard"}}},
    {VIDEO_CODEC, {"video_codec", {"AVC/H.264", "HEVC/H.265", "AV1"}}},
    {FULLSCREEN, {"fullscreen"}},
    {PLAYER_HWDEC, {"player_hwdec"}},
    {TEXTURE_CACHE_NUM, {"texture_cache_num"}},
};

AppConfig::AppConfig() = default;

void AppConfig::init() {
    brls::Logger::info("() init {}", AppVersion::getPlatform(), AppVersion::getVersion());

    const std::string path = this->configDir() + "/config.json";
    std::ifstream readFile(path);
    if (readFile.is_open()) {
        try {
            nlohmann::json::parse(readFile).get_to(*this);
            brls::Logger::info("Load config from: {}", path);
        } catch (const std::exception& e) {
            brls::Logger::error("AppConfig::load: {}", e.what());
        }
    }

    // 初始化是否全屏，必须在创建窗口前设置此值
    VideoContext::FULLSCREEN = this->getItem(FULLSCREEN, false);

    // 初始化i18n
    std::set<std::string> i18nSet{
        brls::LOCALE_AUTO,
        brls::LOCALE_EN_US,
        brls::LOCALE_ZH_HANS,
        brls::LOCALE_ZH_HANT,
    };
    std::string appLang = this->getItem(APP_LANG, brls::LOCALE_AUTO);
    if (appLang != brls::LOCALE_AUTO && i18nSet.count(appLang)) {
        brls::Platform::APP_LOCALE_DEFAULT = appLang;
    } else {
#ifndef __SWITCH__
        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ZH_HANS;
#endif
    }

    // 初始化一些在创建窗口之后才能初始化的内容
    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
        // 初始化主题
        std::string appTheme = this->getItem(APP_THEME, std::string("auto"));
        if (appTheme == "light") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
        } else if (appTheme == "dark") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        }

        // 初始化纹理缓存数量
        brls::TextureCache::instance().cache.setCapacity(getItem(TEXTURE_CACHE_NUM, 200));

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
        // 设置窗口最小尺寸
        brls::Application::getPlatform()->setWindowSizeLimits(MINIMUM_WINDOW_WIDTH, MINIMUM_WINDOW_HEIGHT, 0, 0);
#endif
    });

    // init custom font path
    brls::FontLoader::USER_FONT_PATH = configDir() + "/font.ttf";
    brls::FontLoader::USER_ICON_PATH = configDir() + "/icon.ttf";
    if (access(brls::FontLoader::USER_ICON_PATH.c_str(), F_OK) == -1) {
        // 自定义字体不存在，使用内置字体
        std::string icon = getItem(KEYMAP, std::string("xbox"));
        if (icon == "xbox") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_xbox.ttf");
        } else if (icon == "ps") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
        } else {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
        }
    }

    for (auto& u : this->users) {
        if (u.id == this->user_id) {
            this->user = u;
            break;
        }
    }
    brls::Logger::debug("init finish");
}

void AppConfig::save() {
    const std::string path = this->configDir() + "/config.json";
    std::filesystem::create_directories(this->configDir());
    std::ofstream f(path);
    if (f.is_open()) {
        nlohmann::json j;
        to_json(j, *this);
        f << j.dump(2);
        f.close();
    }
}

std::string AppConfig::configDir() {
#if __SWITCH__
    return "sdmc:/switch/Jellyfin";
#elif _WIN32
    return std::string(getenv("LOCALAPPDATA")) + "\\Jellyfin";
#elif __linux__
    std::string(getenv("HOME")) + "/.config/Jellyfin";
#elif __APPLE__
    return std::string(getenv("HOME")) + "/Library/Application Support/Jellyfin";
#else
#endif
}

int AppConfig::getOptionIndex(const Item item) {
    auto& o = settingMap[item];
    if (setting.contains(o.key)) {
        try {
            std::string value = this->setting.at(o.key);
            for (size_t i = 0; i < o.options.size(); ++i)
                if (o.options[i] == value) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", o.key, e.what());
        }
    }
    return o.defaultOption;
}

bool AppConfig::addServer(const AppServer& s) {
    bool found = false;
    for (auto& o : this->servers) {
        if (s.id == o.id) {
            o.name = s.name;
            o.version = s.version;
            o.os = s.os;
            o.urls.push_back(s.urls.back());
            found = true;
            break;
        }
    }
    if (!found) this->servers.push_back(s);

    this->server_url = s.urls.back();
    this->save();
    return found;
}

bool AppConfig::addUser(const AppUser& u) {
    bool found = false;
    for (auto& o : this->users) {
        if (o.id == u.id) {
            o.name = u.name;
            o.access_token = u.access_token;
            o.server_id = u.server_id;
            found = true;
            break;
        }
    }
    if (!found) this->users.push_back(u);

    this->user_id = u.id;
    this->user = u;
    this->save();
    return found;
}