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

struct AppUser {
    std::string id;
    std::string name;
    std::string access_token;
    std::string server_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppUser, id, name, access_token, server_id);

struct AppServer {
    std::string name;
    std::string id;
    std::string version;
    std::string os;
    std::vector<std::string> urls;
    std::vector<AppUser> users;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppServer, id, name, version, os, urls);

class AppConfig : public brls::Singleton<AppConfig> {
public:
    enum SettingItem {
        SERVERS,
        USERS,
        FULLSCREEN,
        APP_THEME,
        APP_LANG,
        KEYMAP,
        VIDEO_CODEC,
        PLAYER_HWDEC,
        TEXTURE_CACHE_NUM,
    };

    using Servers = std::vector<AppServer>;
    using Users = std::vector<AppUser>;

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

    bool addServer(const AppServer& s);
    bool addUser(const AppUser& u);
    std::string getAccessToken() const { return this->access_token; }
    std::string getServerUrl() const { return this->server_url; }

private:
    static std::unordered_map<SettingItem, OptionItem> settingMap;

    std::string access_token;
    std::string server_url;
    nlohmann::json setting;
};