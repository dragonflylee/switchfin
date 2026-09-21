#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/theme.hpp>
#include <api/jellyfin/system.hpp>
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
    bool is_admin = false;
    jellyfin::UserConfig config;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppUser, id, name, access_token, server_id);

struct AppServer {
    std::string name;
    std::string id;
    std::vector<std::string> urls;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppServer, id, name, urls);

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
#ifdef PS5_NATIVE_GPU
#else
    using UserIter = std::vector<AppUser>::iterator;

#endif
public:
    enum Item {
        FULLSCREEN,
        OVERCLOCK,
        APP_THEME,
        APP_LANG,
        APP_UI_SCALE,
        AUDIO_CHANNELS,
        KEYMAP,
        WINDOW_STATE,
        TRANSCODEC,
        FORCE_DIRECTPLAY,
        MAXDAY_NEXTUP,
        OSD_ON_TOGGLE,
        TOUCH_GESTURE,
        CLIP_POINT,
        SYNC_SETTING,
        MPV_VO,
        MPV_RENDER,
        PLAYER_BOTTOM_BAR,
        PLAYER_LOW_QUALITY,
        PLAYER_INMEMORY_CACHE,
        PLAYER_SPEED,
        PLAYER_HWDEC,
        PLAYER_HWDEC_CUSTOM,
        PLAYER_ASPECT,
        PLAYER_SUBS_FALLBACK,
#ifdef PS5_NATIVE_GPU
        PS5_SUBTITLE_SIZE,
        PS5_SUBTITLE_MARGIN,
#endif
        PLAYER_TV_MODE,
        DANMAKU,
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
        SHOW_FPS,
        SWAP_INTERVAL,
        APP_SWAP_ABXY,  // A-B 交换 和 X-Y 交换
        TEXTURE_CACHE_NUM,
        REQUEST_THREADS,
        REQUEST_TIMEOUT,
        HTTP_PROXY_STATUS,
        HTTP_PROXY,

        DOWNLOAD_QUALITY,
#ifdef PS5_NATIVE_GPU
        DOWNLOAD_LOCATION,  // chosen download-root path ("" = default /data)
#endif

        KEY_REFRESH,        // 刷新快捷键
        KEY_LAST,           // 上一个Tab快捷键
        KEY_NEXT,           // 下一个Tab快捷键
        KEY_VOLUME_UP,      // 音量增大快捷键
        KEY_VOLUME_DOWN,    // 音量减小快捷键
        KEY_VIDEO_PROFILE,  // 视频详情快捷键
        KEY_DANMAKU,        // 弹幕快捷键
        KEY_FORWARD,        // 快进快捷键
        KEY_REWIND,         // 快退快捷键
        KEY_SETTING,        // 设置快捷键
        KEY_VIDEO_QUALITY,  // 视频清晰度菜单快捷键
        KEY_VIDEO_SPEED,    // 视频倍速菜单快捷键
        KEY_VIDEO_OSD,      // 切换OSD显示
        KEY_VIDEO_PAUSE,    // 视频播放暂停快捷键
    };

    AppConfig() = default;

    bool init();
    void initThemes();
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
    void addUser(const AppUser& u, const std::string& url);
    bool removeServer(const std::string& id);
    bool removeUser(const std::string& id);
    const std::string& getDeviceId() { return this->device; }
    std::string getAuth(const std::string& token = "");
    const std::string& getUserId() const { return this->user_id; }
#ifdef PS5_NATIVE_GPU
    const std::string& getUserName() const { return currentUser().name; }
    const std::string& getToken() const { return currentUser().access_token; }
#else
    const std::string& getUserName() const { return this->user->name; }
    const std::string& getToken() const { return this->user->access_token; }
#endif
    const std::string& getUrl() const { return this->server_url; }
#ifdef PS5_NATIVE_GPU
    // Captured on the UI thread; workers only read the atomic cancellation flag.
    std::shared_ptr<std::atomic_bool> requestCancellation() const { return requestCancelled; }
    void invalidateRequests() {
        auto next = std::make_shared<std::atomic_bool>(false);
        requestCancelled->store(true);
        requestCancelled = std::move(next);
    }
    // Completion guards must not dereference user: erasing a saved account
    // can invalidate that iterator while an earlier HTTP request is pending.
    bool matchesPlaybackSession(const std::string& url, const std::string& id, const std::string& token) const {
        if (url != server_url || id != user_id || id.empty()) return false;
        for (const auto& saved : users)
            if (saved.id == id) return saved.access_token == token;
        return false;
    }
    bool isAdmin() const { return currentUser().is_admin; }
    const jellyfin::UserConfig& userConfig() const { return currentUser().config; }
#else
    bool isAdmin() const { return this->user->is_admin; }
    const jellyfin::UserConfig& userConfig() const { return this->user->config; }
#endif
    void addRemote(const AppRemote& r);
    void updateRemote(size_t index, const AppRemote& r);
    void removeRemote(size_t index);
    const std::vector<AppRemote>& getRemotes() const { return this->remotes; }
    const std::vector<AppServer>& getServers() const { return this->servers; }
    const std::vector<AppUser> getUsers(const std::string& id) const;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AppConfig, user_id, device, users, servers, setting, remotes);

    inline static bool SYNC = true;

private:
    static std::unordered_map<Item, Option> settingMap;

#ifdef PS5_NATIVE_GPU
    // Saved accounts can be erased or reallocated while views finish closing.
    // Never keep an iterator into that vector across a configuration change.
    const AppUser& currentUser() const {
        for (const auto& saved : users)
            if (!user_id.empty() && saved.id == user_id) return saved;
        static const AppUser empty{};
        return empty;
    }
    std::shared_ptr<std::atomic_bool> requestCancelled = std::make_shared<std::atomic_bool>(false);
#else
    UserIter user;
#endif
    std::string user_id;
    std::string server_url;
    std::string device;
    std::string device_name;
    std::vector<AppUser> users;
    std::vector<AppServer> servers;
    std::vector<AppRemote> remotes;
    nlohmann::json setting = {};

    void addColor(const brls::ThemeVariant tv, const std::string& name, NVGcolor defaultColor);
#ifdef PS5_NATIVE_GPU
};

#else
};
#endif
