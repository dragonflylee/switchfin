#ifdef __SWITCH__
#include <switch.h>
#include "utils/overclock.hpp"
#else
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#if defined(_WIN32)
#include <shlobj.h>
#endif
#endif

#include <fstream>
#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#elif !defined(USE_LIBROMFS)
#error "Failed to include <filesystem> header!"
#endif
#include <set>
#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include "api/jellyfin.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "view/mpv_core.hpp"
#include "view/danmaku_core.hpp"

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;

std::unordered_map<AppConfig::Item, AppConfig::Option> AppConfig::settingMap = {
    {APP_THEME, {"app_theme", {"auto", "light", "dark"}}},
    {APP_LANG, {"app_lang", {brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_ZH_HANS, brls::LOCALE_ZH_HANT,
                                brls::LOCALE_DE, "cs", "uk-UA"}}},
    {KEYMAP, {"keymap", {"xbox", "ps", "keyboard"}}},
    {TRANSCODEC, {"transcodec", {"h264", "hevc", "av1"}}},
    {FORCE_DIRECTPLAY, {"force_directplay"}},
    {VIDEO_QUALITY,
        {
            "video_quality",
            {"Auto", "1080p - 60Mbps", "1080p - 40Mbps", "1080p - 20Mbps", "720p - 8Mbps", "720p - 6Mbps",
                "480p - 3Mbps", "480P - 1Mbps"},
            {0, 60, 40, 20, 8, 6, 3, 1},
        }},
    {FULLSCREEN, {"fullscreen"}},
    {OSD_ON_TOGGLE, {"osd_on_toggle"}},
    {TOUCH_GESTURE, {"touch_gesture"}},
    {CLIP_POINT, {"clip_point"}},
    {OVERCLOCK, {"overclock"}},
    {PLAYER_BOTTOM_BAR, {"player_bottom_bar"}},
    {PLAYER_SEEKING_STEP, {"player_seeking_step", {"5", "10", "15", "30"}, {5, 10, 15, 30}}},
    {PLAYER_LOW_QUALITY, {"player_low_quality"}},
    {PLAYER_INMEMORY_CACHE,
        {
            "player_inmemory_cache",
            {"0MB", "10MB", "20MB", "50MB", "100MB", "200MB", "500MB"},
            {0, 10, 20, 50, 100, 200, 500},
        }},
    {PLAYER_HWDEC, {"player_hwdec"}},
    {PLAYER_HWDEC_CUSTOM, {"player_hwdec_custom"}},
    {PLAYER_ASPECT, {"player_aspect", {"auto", "stretch", "crop", "4:3", "16:9"}}},
    {DANMAKU_STYLE_AREA, {"danmaku_style_area", {"1/4", "1/2", "3/4", "1"}, {25, 50, 75, 100}}},
    {DANMAKU_STYLE_ALPHA,
        {
            "danmaku_style_alpha",
            {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"},
            {10, 20, 30, 40, 50, 60, 70, 80, 90, 100},
        }},
    {DANMAKU_STYLE_FONTSIZE,
        {"danmaku_style_fontsize", {"50%", "75%", "100%", "125%", "150%", "175%"}, {15, 22, 30, 37, 45, 50}}},
    {DANMAKU_STYLE_FONT, {"danmaku_style_font", {"stroke", "incline", "shadow", "pure"}}},
    {DANMAKU_STYLE_LINE_HEIGHT,
        {
            "danmaku_style_line_height",
            {"100%", "120%", "140%", "160%", "180%", "200%"},
            {100, 120, 140, 160, 180, 200},
        }},
    {DANMAKU_STYLE_SPEED,
        {
            "danmaku_style_speed",
            {"0.5", "0.75", "1.0", "1.25", "1.5"},
            {150, 125, 100, 75, 50},
        }},
    {DANMAKU_RENDER_QUALITY,
        {
            "danmaku_render_quality",
            {"100%", "95%", "90%", "80%", "70%", "60%", "50%"},
            {100, 95, 90, 80, 70, 60, 50},
        }},
    {ALWAYS_ON_TOP, {"always_on_top"}},
    {SINGLE, {"single"}},
    {TEXTURE_CACHE_NUM, {"texture_cache_num"}},
    {REQUEST_THREADS, {"request_threads", {"1", "2", "4", "8"}, {1, 2, 4, 8}}},
    {REQUEST_TIMEOUT, {"request_timeout", {"1000", "2000", "3000", "5000"}, {1000, 2000, 3000, 5000}}},
    {TLS_VERIFY, {"tls_verify"}},
    {HTTP_PROXY_STATUS, {"http_proxy_status"}},
    {HTTP_PROXY_HOST, {"http_proxy_host"}},
    {HTTP_PROXY_PORT, {"http_proxy_port"}},
};

static std::string generateDeviceId() {
#ifdef __SWITCH__
    AccountUid uid;
    accountInitialize(AccountServiceType_Administrator);
    if (R_FAILED(accountGetPreselectedUser(&uid))) {
        if (R_FAILED(accountTrySelectUserWithoutInteraction(&uid, false))) {
            accountGetLastOpenedUser(&uid);
        }
    }
    accountExit();
    if (accountUidIsValid(&uid)) {
        uint8_t digest[32];
        sha256CalculateHash(digest, &uid, sizeof(uid));
        return misc::hexEncode(digest, sizeof(digest));
    }
#elif defined(_WIN32)
    HW_PROFILE_INFOW profile;
    if (GetCurrentHwProfileW(&profile)) {
        std::vector<char> deviceId(HW_PROFILE_GUIDLEN);
        WideCharToMultiByte(CP_UTF8, 0, profile.szHwProfileGuid, std::wcslen(profile.szHwProfileGuid), deviceId.data(),
            deviceId.size(), nullptr, nullptr);
        return deviceId.data();
    }
#elif defined(__APPLE__)
    io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
    if (ioRegistryRoot) {
        CFStringRef uuidCf = (CFStringRef)IORegistryEntryCreateCFProperty(
            ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
        std::vector<char> deviceId(CFStringGetLength(uuidCf) + 1);
        CFStringGetCString(uuidCf, deviceId.data(), deviceId.size(), kCFStringEncodingMacRoman);
        CFRelease(uuidCf);
        IOObjectRelease(ioRegistryRoot);
        return deviceId.data();
    }
#endif
    return misc::randHex(16);
}

bool AppConfig::init() {
    const std::string path = this->configDir() + "/config.json";
#if !defined(USE_BOOST_FILESYSTEM) || defined(_WIN32)
    std::ifstream f(fs::u8path(path));
#else
    std::ifstream f(path);
#endif
    if (f.is_open()) {
        try {
            nlohmann::json::parse(f).get_to(*this);
            brls::Logger::info("Load config from: {}", path);
        } catch (const std::exception& ex) {
            brls::Logger::error("AppConfig::load: {}", ex.what());
            return false;
        }
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    misc::initCrashDump();

    if (this->getItem(AppConfig::SINGLE, false) && misc::sendIPC(this->ipcSocket(), "{}")) {
        brls::Logger::warning("AppConfig single instance");
        return false;
    }
#endif

    HTTP::TIMEOUT = this->getItem(REQUEST_TIMEOUT, 3000L);
    HTTP::PROXY_STATUS = this->getItem(HTTP_PROXY_STATUS, false);
    HTTP::PROXY_HOST = this->getItem(HTTP_PROXY_HOST, HTTP::PROXY_HOST);
    HTTP::PROXY_PORT = this->getItem(HTTP_PROXY_PORT, HTTP::PROXY_PORT);
    HTTP::TLS_VERIFY = this->getItem(TLS_VERIFY, false);

    // 初始化是否全屏，必须在创建窗口前设置此值
    VideoContext::FULLSCREEN = this->getItem(FULLSCREEN, false);

    // 初始化是否固定显示底部进度条
    MPVCore::BOTTOM_BAR = this->getItem(PLAYER_BOTTOM_BAR, true);
    MPVCore::OSD_ON_TOGGLE = this->getItem(PLAYER_BOTTOM_BAR, true);
    MPVCore::TOUCH_GESTURE = this->getItem(TOUCH_GESTURE, true);
    MPVCore::CLIP_POINT = this->getItem(CLIP_POINT, true);
    // 初始化内存缓存大小
    MPVCore::INMEMORY_CACHE = this->getItem(PLAYER_INMEMORY_CACHE, 10);
    // 是否使用低质量解码
#if defined(__PSV__) || defined(PS4)
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, true);
#else
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, false);
#endif

    // 初始化是否使用硬件加速
    MPVCore::HARDWARE_DEC = this->getItem(PLAYER_HWDEC, true);
    MPVCore::FORCE_DIRECTPLAY = this->getItem(FORCE_DIRECTPLAY, false);
    MPVCore::SEEKING_STEP = this->getItem(PLAYER_SEEKING_STEP, 15);
    MPVCore::VIDEO_CODEC = this->getItem(TRANSCODEC, MPVCore::VIDEO_CODEC);
    MPVCore::VIDEO_QUALITY = this->getItem(VIDEO_QUALITY, 0L);

    // 初始化自定义的硬件加速方案
    MPVCore::PLAYER_HWDEC_METHOD = this->getItem(PLAYER_HWDEC_CUSTOM, MPVCore::PLAYER_HWDEC_METHOD);

    // 初始化视频比例
    MPVCore::VIDEO_ASPECT = this->getItem(PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);

    // 初始化弹幕相关内容
    DanmakuCore::DANMAKU_ON = this->getItem(DANMAKU_ON, true);
    DanmakuCore::DANMAKU_STYLE_AREA = this->getItem(DANMAKU_STYLE_AREA, 100);
    DanmakuCore::DANMAKU_STYLE_ALPHA = this->getItem(DANMAKU_STYLE_ALPHA, 80);
    DanmakuCore::DANMAKU_STYLE_FONTSIZE = this->getItem(DANMAKU_STYLE_FONTSIZE, 30);
    DanmakuCore::DANMAKU_STYLE_LINE_HEIGHT = this->getItem(DANMAKU_STYLE_LINE_HEIGHT, 120);
    DanmakuCore::DANMAKU_STYLE_SPEED = this->getItem(DANMAKU_STYLE_SPEED, 100);

    // 初始化 deviceId
    if (this->device.empty()) this->device = generateDeviceId();

    // 初始化i18n
    brls::Platform::APP_LOCALE_DEFAULT = this->getItem(APP_LANG, brls::LOCALE_AUTO);

    // 初始化一些在创建窗口之后才能初始化的内容
    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
        // 初始化弹幕字体
        std::string danmakuFont = this->configDir() + "/danmaku.ttf";
        // 只在应用模式下加载自定义字体 减少switch上的内存占用
        if (brls::Application::getPlatform()->isApplicationMode() && access(danmakuFont.c_str(), F_OK) != -1 &&
            brls::Application::loadFontFromFile("danmaku", danmakuFont)) {
            // 自定义弹幕字体
            int danmakuFontId = brls::Application::getFont("danmaku");
            nvgAddFallbackFontId(
                brls::Application::getNVGContext(), danmakuFontId, brls::Application::getDefaultFont());
            DanmakuCore::DANMAKU_FONT = danmakuFontId;
        } else {
            // 使用默认弹幕字体
            DanmakuCore::DANMAKU_FONT = brls::Application::getDefaultFont();
        }
        // 初始化主题
        std::string appTheme = this->getItem(APP_THEME, std::string("auto"));
        if (appTheme == "light") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
        } else if (appTheme == "dark") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        }

        // 初始化纹理缓存数量
#if defined(__PSV__) || defined(PS4)
        brls::TextureCache::instance().cache.setCapacity(1);
#else
        brls::TextureCache::instance().cache.setCapacity(getItem(TEXTURE_CACHE_NUM, 200));
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
        // 设置窗口最小尺寸
        brls::Application::getPlatform()->setWindowSizeLimits(MINIMUM_WINDOW_WIDTH, MINIMUM_WINDOW_HEIGHT, 0, 0);
        if (this->getItem(ALWAYS_ON_TOP, false)) {
            brls::Application::getPlatform()->setWindowAlwaysOnTop(true);
        }
#endif

        // i18n 相关
        settingMap[VIDEO_QUALITY].options[0] = brls::getStr("main/player/auto");
    });

#ifdef __SWITCH__
    /// Set Overclock
    if (getItem(AppConfig::OVERCLOCK, false)) {
        SwitchSys::setClock(true);
    };
#endif

    // init custom font path
    brls::FontLoader::USER_FONT_PATH = configDir() + "/font.ttf";
    brls::FontLoader::USER_ICON_PATH = configDir() + "/icon.ttf";
    if (access(brls::FontLoader::USER_ICON_PATH.c_str(), F_OK) == -1) {
        // 自定义字体不存在，使用内置字体
#if defined(__PSV__) || defined(PS4)
        brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
#else
        std::string icon = getItem(KEYMAP, std::string("xbox"));
        if (icon == "xbox") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_xbox.ttf");
        } else if (icon == "ps") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
        } else {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
        }
#endif
    }

    brls::Logger::info("init {} v{}-{} device {} from {}", AppVersion::getPlatform(), AppVersion::getVersion(),
        AppVersion::getCommit(), this->device, path);
    return true;
}

void AppConfig::save() {
    try {
        std::string dir = this->configDir();
        fs::create_directories(dir);
#if !defined(USE_BOOST_FILESYSTEM) || defined(_WIN32)
        std::ofstream f(fs::u8path(dir + "/config.json"));
#else
        std::ofstream f(dir + "/config.json");
#endif
        if (f.is_open()) {
            nlohmann::json j(*this);
            f << j.dump(2);
            f.close();
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("AppConfig save: {}", ex.what());
    }
}

bool AppConfig::checkLogin() {
    for (auto& u : this->users) {
        if (u.id == this->user_id) {
            HTTP::Header header = {this->getDevice(u.access_token)};
            try {
                HTTP::get(this->server_url + jellyfin::apiInfo, header, HTTP::Timeout{});
                this->user = u;
                return true;
            } catch (const std::exception& ex) {
                brls::Logger::warning("AppConfig checkLogin: {}", ex.what());
                return false;
            }
        }
    }
    return false;
}

bool AppConfig::checkDanmuku() {
    jellyfin::getJSON(
        [](const jellyfin::PluginList& plugins) {
            for (auto& p : plugins) {
                if (p.Name == "Danmu") {
                    DanmakuCore::PLUGIN_ACTIVE = true;
                    brls::Logger::info("Danmaku plugin found: {}", p.Version);
                    return;
                }
            }
            DanmakuCore::PLUGIN_ACTIVE = false;
            brls::Logger::warning("Danmaku plugin not found");
        },
        [](const std::string& err) {
            DanmakuCore::PLUGIN_ACTIVE = false;
            brls::Logger::error("AppConfig checkDanmuku: {}", err);
        },
        jellyfin::apiPlugins);
    return false;
}

std::string AppConfig::configDir() {
#if __SWITCH__
    return fmt::format("sdmc:/switch/{}", AppVersion::getPackageName());
#elif defined(PS4)
    return fmt::format("/data/{}", AppVersion::getPackageName());
#elif defined(__PSV__)
    return fmt::format("ux0:/data/{}", AppVersion::getPackageName());
#elif _WIN32
    WCHAR wpath[MAX_PATH];
    std::vector<char> lpath(MAX_PATH);
    SHGetSpecialFolderPathW(0, wpath, CSIDL_LOCAL_APPDATA, false);
    WideCharToMultiByte(CP_UTF8, 0, wpath, std::wcslen(wpath), lpath.data(), lpath.size(), nullptr, nullptr);
    return fmt::format("{}\\{}", lpath.data(), AppVersion::getPackageName());
#elif __linux__
    char* config_home = getenv("XDG_CONFIG_HOME");
    if (config_home) return fmt::format("{}/{}", config_home, AppVersion::getPackageName());
    return fmt::format("{}/.config/{}", getenv("HOME"), AppVersion::getPackageName());
#elif __APPLE__
    return fmt::format("{}/Library/Application Support/{}", getenv("HOME"), AppVersion::getPackageName());
#endif
}

std::string AppConfig::ipcSocket() {
#ifdef _WIN32
    return "\\\\.\\pipe\\" + AppVersion::getPackageName();
#else
    return fmt::format("{}/{}.sock", configDir(), AppVersion::getPackageName());
#endif
}

void AppConfig::checkRestart(char* argv[]) {
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (brls::DesktopPlatform::RESTART_APP) {
        brls::Logger::info("Restart app {}", argv[0]);
        execv(argv[0], argv);
    }
#endif
}

int AppConfig::getOptionIndex(const Item item, int default_index) const {
    auto it = settingMap.find(item);
    if (setting.contains(it->second.key)) {
        try {
            std::string value = this->setting.at(it->second.key);
            for (size_t i = 0; i < it->second.options.size(); ++i)
                if (it->second.options[i] == value) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", it->second.key, e.what());
        }
    }
    return default_index;
}

int AppConfig::getValueIndex(const Item item, int default_index) const {
    auto it = settingMap.find(item);
    if (setting.contains(it->second.key)) {
        try {
            long value = this->setting.at(it->second.key);
            for (size_t i = 0; i < it->second.values.size(); ++i)
                if (it->second.values[i] == value) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", it->second.key, e.what());
        }
    }
    return default_index;
}

bool AppConfig::addServer(const AppServer& s) {
    this->server_url = s.urls.front();

    for (auto& o : this->servers) {
        if (s.id == o.id) {
            o.name = s.name;
            o.version = s.version;
            o.os = s.os;
            // remove old url
            for (auto it = o.urls.begin(); it != o.urls.end(); ++it) {
                if (it->compare(this->server_url) == 0) {
                    it = o.urls.erase(it);
                    break;
                }
            }
            o.urls.insert(o.urls.begin(), this->server_url);
            this->save();
            return true;
        }
    }
    this->servers.push_back(s);
    this->save();
    return false;
}

bool AppConfig::addUser(const AppUser& u, const std::string& url) {
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

    this->server_url = url;
    this->user_id = u.id;
    this->user = u;
    this->save();
    return found;
}

bool AppConfig::removeServer(const std::string& id) {
    for (auto it = this->servers.begin(); it != this->servers.end(); ++it) {
        if (it->id == id) {
            this->servers.erase(it);
            this->save();
            return true;
        }
    }
    return false;
}

bool AppConfig::removeUser(const std::string& id) {
    for (auto it = this->users.begin(); it != this->users.end(); ++it) {
        if (it->id == id) {
            this->users.erase(it);
            this->save();
            return true;
        }
    }
    return false;
}

std::string AppConfig::getDevice(const std::string& token) {
    if (token.empty())
        return fmt::format(
            "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
            AppVersion::getPackageName(), AppVersion::getDeviceName(), this->device, AppVersion::getVersion());
    else
        return fmt::format(
            "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\", "
            "Token=\"{}\"",
            AppVersion::getPackageName(), AppVersion::getDeviceName(), this->device, AppVersion::getVersion(), token);
}

const std::vector<AppServer> AppConfig::getServers() const {
    std::vector<AppServer> list(this->servers);
    for (auto& u : this->users) {
        for (auto& s : list) {
            if (u.server_id == s.id) {
                s.users.push_back(u);
            }
        }
    }
    return list;
}
