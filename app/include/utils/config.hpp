#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>
#include <atomic>

const long default_timeout = 3000L;
const int default_seekstep = 15;

class AppVersion {
public:
    static std::string getVersion();
    static std::string getPlatform();
    static std::string getDeviceName();
    static std::string getPackageName();
    static std::string getCommit();
    static bool needUpdate(std::string latestVersion);
    static void checkUpdate(int delay = 2000, bool showUpToDateDialog = false);

    inline static std::shared_ptr<std::atomic_bool> updating = std::make_shared<std::atomic_bool>(true);
    inline static std::string git_repo = "dragonflylee/switchfin";
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
        OVERCLOCK,
        APP_THEME,
        APP_LANG,
        KEYMAP,
        TRANSCODEC,
        FORCE_DIRECTPLAY,
        MAXBITRATE,
        OSD_ON_TOGGLE,
        PLAYER_BOTTOM_BAR,
        PLAYER_SEEKING_STEP,
        PLAYER_HWDEC,
        PLAYER_HWDEC_CUSTOM,
        TEXTURE_CACHE_NUM,
        REQUEST_THREADS,
        REQUEST_TIMEOUT,
    };

    AppConfig();

    void init();
    void save();
    bool checkLogin();

    std::string configDir();
    void checkRestart(char* argv[]);

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
        std::vector<long> values;
    };

    int getOptionIndex(const Item item, int default_index = 0) const;
    int getValueIndex(const Item item, int default_index = 0) const;
    inline const Option& getOptions(const Item item) const { return settingMap[item]; }

    bool addServer(const AppServer& s);
    bool addUser(const AppUser& u, const std::string& url);
    const std::string& getDeviceId() { return this->device; }
    std::string getDevice(const std::string& token = "");
    const AppUser& getUser() const { return this->user; }
    const std::string& getUrl() const { return this->server_url; }
    const std::vector<AppServer> getServers() const;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig, user_id, server_url, device, users, servers, setting);

private:
    static std::unordered_map<Item, Option> settingMap;

    AppUser user;
    std::string user_id;
    std::string server_url;
    std::string device;
    std::vector<AppUser> users;
    std::vector<AppServer> servers;
    nlohmann::json setting = {};
};