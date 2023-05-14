#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

class AppVersion : public brls::Singleton<AppVersion> {
public:
    std::string git_commit, git_tag;
    AppVersion();

    std::string getPlatform();
    bool needUpdate(std::string latestVersion);
    void checkUpdate(int delay = 2000, bool showUpToDateDialog = false);
};

class AppConfig : public brls::Singleton<AppConfig> {
public:
    enum SettingItem {
        FULLSCREEN,
        APP_THEME,
        APP_LANG,
        KEYMAP,
        VIDEO_CODEC,
        PLAYER_HWDEC,
        TEXTURE_CACHE_NUM,
    };

    AppConfig();

    void init();
    void save();
    std::string configDir();

    template <typename T>
    T getItem(const SettingItem item, T defaultValue) {
        auto& o = settingMap[item];
        try {
            if (!setting.contains(o.key)) return defaultValue;
            return this->setting.at(o.key).get<T>();
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", o.key, e.what());
            return defaultValue;
        }
    }

    template <typename T>
    void setItem(const SettingItem item, T data) {
        auto& o = settingMap[item];
        this->setting[o.key] = data;
        this->save();
    }

    struct OptionItem {
        std::string key;
        std::vector<std::string> options;
        size_t defaultOption;
    };

    int getOptionIndex(const SettingItem item);
    inline OptionItem getOptions(const SettingItem item) { return settingMap[item]; }

private:
    static std::unordered_map<SettingItem, OptionItem> settingMap;

    nlohmann::json setting;
};