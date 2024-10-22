#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>
#include <atomic>

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppServer, id, name, version, os, urls);

struct AppRemote {
    std::string name;
    std::string url;
    std::string user;
    std::string passwd;
    std::string user_agent;
};
inline void to_json(nlohmann::json& nlohmann_json_j, const AppRemote& nlohmann_json_t) {
    if (!nlohmann_json_t.user.empty()) nlohmann_json_j["user"] = nlohmann_json_t.user;
    if (!nlohmann_json_t.passwd.empty()) nlohmann_json_j["passwd"] = nlohmann_json_t.passwd;
    if (!nlohmann_json_t.user_agent.empty()) nlohmann_json_j["user_agent"] = nlohmann_json_t.user_agent;
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, name, url))
}
inline void from_json(const nlohmann::json& nlohmann_json_j, AppRemote& nlohmann_json_t) {
    if (nlohmann_json_j.contains("user")) nlohmann_json_j["user"].get_to(nlohmann_json_t.user);
    if (nlohmann_json_j.contains("passwd")) nlohmann_json_j["passwd"].get_to(nlohmann_json_t.passwd);
    if (nlohmann_json_j.contains("user_agent")) nlohmann_json_j["user_agent"].get_to(nlohmann_json_t.user_agent);
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, name, url))
}

class AppConfig : public brls::Singleton<AppConfig> {
public:
    enum Item {
        FULLSCREEN,
        OVERCLOCK,
        APP_THEME,
        APP_LANG,
        APP_UPDATE,
        KEYMAP,
        WINDOW_STATE,
        TRANSCODEC,
        FORCE_DIRECTPLAY,
        VIDEO_QUALITY,
        OSD_ON_TOGGLE,
        TOUCH_GESTURE,
        CLIP_POINT,
        SYNC_SETTING,
        PLAYER_BOTTOM_BAR,
        PLAYER_SEEKING_STEP,
        PLAYER_LOW_QUALITY,
        PLAYER_INMEMORY_CACHE,
        PLAYER_HWDEC,
        PLAYER_HWDEC_CUSTOM,
        PLAYER_ASPECT,
        PLAYER_SUBS_FALLBACK,
        DANMAKU_ON,
        DANMAKU_STYLE_AREA,
        DANMAKU_STYLE_ALPHA,
        DANMAKU_STYLE_FONTSIZE,
        DANMAKU_STYLE_LINE_HEIGHT,
        DANMAKU_STYLE_SPEED,
        DANMAKU_STYLE_FONT,
        DANMAKU_RENDER_QUALITY,
        ALWAYS_ON_TOP,
        SINGLE,
        APP_SWAP_ABXY,  // A-B 交换 和 X-Y 交换
        TEXTURE_CACHE_NUM,
        REQUEST_THREADS,
        REQUEST_TIMEOUT,
        HTTP_PROXY_STATUS,
        HTTP_PROXY_HOST,
        HTTP_PROXY_PORT,
    };

    AppConfig() = default;

    bool init();
    void save();
    bool checkLogin();
    /// @brief 检查是否安装Danmuku插件
    bool checkDanmuku();

    std::string configDir();
    std::string ipcSocket();
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
    bool removeServer(const std::string& id);
    bool removeUser(const std::string& id);
    const std::string& getDeviceId() { return this->device; }
    std::string getDevice(const std::string& token = "");
    const AppUser& getUser() const { return this->user; }
    const std::string& getUrl() const { return this->server_url; }
    const std::vector<AppRemote>& getRemotes() const { return this->remotes; }
    const std::vector<AppServer> getServers() const;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
        AppConfig, user_id, server_url, device, users, servers, setting, remotes);

    inline static bool SYNC = true;

private:
    static std::unordered_map<Item, Option> settingMap;

    AppUser user;
    std::string user_id;
    std::string server_url;
    std::string device;
    std::vector<AppUser> users;
    std::vector<AppServer> servers;
    std::vector<AppRemote> remotes;
    nlohmann::json setting = {};
};