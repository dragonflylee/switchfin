#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

class AppVersion{
public:
    static std::string getVersion();
    static std::string getPlatform();
    static std::string getDeviceName();
    static std::string getPackageName();
    static bool needUpdate(std::string latestVersion);
    static void checkUpdate(int delay = 2000, bool showUpToDateDialog = false);

private:
    static std::string git_commit;
    static std::string git_tag;
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
    enum Item {
        FULLSCREEN,
        APP_THEME,
        APP_LANG,
        KEYMAP,
        VIDEO_CODEC,
        PLAYER_HWDEC,
        TEXTURE_CACHE_NUM,
        REQUEST_THREADS,
    };

    AppConfig();

    void init();
    void save();
    std::string configDir();

    template <typename T>
    T getItem(const Item item, T defaultValue) {
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
    void setItem(const Item item, T data) {
        auto& o = settingMap[item];
        this->setting[o.key] = data;
        this->save();
    }

    struct Option {
        std::string key;
        std::vector<std::string> options;
        size_t selected;
    };

    int getOptionIndex(const Item item) const;
    inline const Option& getOptions(const Item item) const { return settingMap[item]; }

    bool addServer(const AppServer& s);
    bool addUser(const AppUser& u);
    const std::string& getAccessToken() const { return this->user.access_token; }
    const std::string& getUserId() const { return this->user.id; }
    const std::string& getUsername() const { return this->user.name; }
    const std::string& getServerUrl() const { return this->server_url; }
    const std::vector<AppServer>& getServers() const { return this->servers; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig, user_id, server_url, device, users, servers, setting);

private:
    static std::unordered_map<Item, Option> settingMap;

    AppUser user;
    std::string user_id;
    std::string server_url;
    std::string device;
    std::vector<AppUser> users;
    std::vector<AppServer> servers;
    nlohmann::json setting;
};