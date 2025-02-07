#ifdef __SWITCH__
#include <switch.h>
#include "utils/overclock.hpp"
#elif defined(__PSV__)
#elif defined(__PS4__)
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <arpa/inet.h>

extern "C" {
extern int ps4_mpv_use_precompiled_shaders;
extern int ps4_mpv_dump_shaders;
extern in_addr_t primary_dns;
extern in_addr_t secondary_dns;
}
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
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
#include <borealis/views/edit_text_dialog.hpp>
#include "api/jellyfin.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "view/mpv_core.hpp"
#include "view/danmaku_core.hpp"
#include "view/video_view.hpp"

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;

std::unordered_map<AppConfig::Item, AppConfig::Option> AppConfig::settingMap = {
    {APP_THEME, {"app_theme", {"auto", "light", "dark"}}},
    {APP_LANG, {"app_lang", {brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_ZH_HANS, brls::LOCALE_ZH_HANT,
                                brls::LOCALE_JA, brls::LOCALE_Ko, brls::LOCALE_DE, "cs", "uk-UA", "vi_VN"}}},
    {APP_UPDATE, {"app_update"}},
    {KEYMAP, {"keymap", {"xbox", "ps", "keyboard"}}},
    {WINDOW_STATE, {"window_state"}},
    {TRANSCODEC, {"transcodec", {"h264", "hevc", "av1"}}},
    {FORCE_DIRECTPLAY, {"force_directplay"}},        
    {FULLSCREEN, {"fullscreen"}},
    {OSD_ON_TOGGLE, {"osd_on_toggle"}},
    {TOUCH_GESTURE, {"touch_gesture"}},
    {CLIP_POINT, {"clip_point"}},
    {SYNC_SETTING, {"sync_setting"}},
    {OVERCLOCK, {"overclock"}},
    {PLAYER_BOTTOM_BAR, {"player_bottom_bar"}},
    {PLAYER_LOW_QUALITY, {"player_low_quality"}},
    {PLAYER_SUBS_FALLBACK, {"player_subs_fallback"}},
    {PLAYER_INMEMORY_CACHE,
        {
            "player_inmemory_cache",
            {"0MB", "10MB", "20MB", "50MB", "100MB", "200MB", "500MB"},
            {0, 10, 20, 50, 100, 200, 500},
        }},
    {PLAYER_HWDEC, {"player_hwdec"}},
    {PLAYER_HWDEC_CUSTOM, {"player_hwdec_custom"}},
    {PLAYER_ASPECT, {"player_aspect", {"auto", "stretch", "crop", "4:3", "16:9"}}},
    {DANMAKU, {"danmaku"}},
    {DANMAKU_ON, {"danmaku_on"}},
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
    {FPS, {"fps"}},
    {APP_SWAP_ABXY, {"app_swap_abxy"}},
    {TEXTURE_CACHE_NUM, {"texture_cache_num"}},
    {REQUEST_THREADS, {"request_threads", {"1", "2", "4", "8"}, {1, 2, 4, 8}}},
    {REQUEST_TIMEOUT, {"request_timeout", {"1000", "2000", "3000", "5000"}, {1000, 2000, 3000, 5000}}},
    {HTTP_PROXY_STATUS, {"http_proxy_status"}},
    {HTTP_PROXY, {"http_proxy"}},
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

#if defined(_WIN32) && !defined(_WINRT_)
    misc::initCrashDump();
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (this->getItem(AppConfig::SINGLE, false) && misc::sendIPC(this->ipcSocket(), "{}")) {
        brls::Logger::warning("AppConfig single instance");
        return false;
    }
    // 加载窗口位置
    auto wstate = this->getItem(AppConfig::WINDOW_STATE, std::string{""});
    if (wstate.size() > 0) {
        int hXPos, hYPos, monitor;
        uint32_t hWidth, hHeight;
        sscanf(wstate.c_str(), "%d,%ux%u,%dx%d", &monitor, &hWidth, &hHeight, &hXPos, &hYPos);
        if (hWidth > 0 && hHeight > 0) {
            VideoContext::sizeH = hHeight;
            VideoContext::sizeW = hWidth;
            VideoContext::posX = (float)hXPos;
            VideoContext::posY = (float)hYPos;
            VideoContext::monitorIndex = monitor;
        }
    }
    // 窗口将要关闭时, 保存窗口状态配置
    brls::Application::getExitEvent()->subscribe([this]() {
        if (std::isnan(VideoContext::posX) || std::isnan(VideoContext::posY)) return;
        if (VideoContext::FULLSCREEN) return;
        auto videoContext = brls::Application::getPlatform()->getVideoContext();
        uint32_t width = VideoContext::sizeW;
        uint32_t height = VideoContext::sizeH;
        if (width == 0) width = brls::Application::ORIGINAL_WINDOW_WIDTH;
        if (height == 0) height = brls::Application::ORIGINAL_WINDOW_HEIGHT;
        this->setItem(AppConfig::WINDOW_STATE, fmt::format("{},{}x{},{}x{}", videoContext->getCurrentMonitorIndex(),
                                                   width, height, (int)VideoContext::posX, (int)VideoContext::posY));
        this->save();
    });
#elif defined(__PS4__)
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0) brls::Logger::error("cannot load net module");
    primary_dns = inet_addr("223.5.5.5");
    secondary_dns = inet_addr("1.1.1.1");
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders = 0;
    // 在加载第一帧之后隐藏启动画面
    brls::sync([]() { sceSystemServiceHideSplashScreen(); });
#endif

    AppConfig::SYNC = this->getItem(SYNC_SETTING, true);

    HTTP::TIMEOUT = this->getItem(REQUEST_TIMEOUT, 3000L);
    HTTP::PROXY_STATUS = this->getItem(HTTP_PROXY_STATUS, false);
    HTTP::PROXY = this->getItem(HTTP_PROXY, std::string("http://192.168.1.1:1080"));

    // 初始化是否全屏，必须在创建窗口前设置此值
    VideoContext::FULLSCREEN = this->getItem(FULLSCREEN, false);

    // 初始化是否固定显示底部进度条
    MPVCore::BOTTOM_BAR = this->getItem(PLAYER_BOTTOM_BAR, true);
    MPVCore::OSD_ON_TOGGLE = this->getItem(OSD_ON_TOGGLE, true);
    MPVCore::TOUCH_GESTURE = this->getItem(TOUCH_GESTURE, true);
    MPVCore::CLIP_POINT = this->getItem(CLIP_POINT, true);
    // 初始化内存缓存大小
    MPVCore::INMEMORY_CACHE = this->getItem(PLAYER_INMEMORY_CACHE, 10);
    // 是否使用低质量解码
#if defined(__PSV__) || defined(__PS4__) || defined(__SWITCH__)
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, true);
#else
    MPVCore::LOW_QUALITY = this->getItem(PLAYER_LOW_QUALITY, false);
#endif
    MPVCore::SUBS_FALLBACK = this->getItem(PLAYER_SUBS_FALLBACK, true);

    // 初始化是否使用硬件加速
    MPVCore::HARDWARE_DEC = this->getItem(PLAYER_HWDEC, true);
    MPVCore::FORCE_DIRECTPLAY = this->getItem(FORCE_DIRECTPLAY, false);
    MPVCore::VIDEO_CODEC = this->getItem(TRANSCODEC, MPVCore::VIDEO_CODEC);

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

    brls::Application::setFPSStatus(this->getItem(FPS, false));

    // 初始化一些在创建窗口之后才能初始化的内容
    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
        // 是否交换按键
        if (this->getItem(APP_SWAP_ABXY, false)) {
            // 对于 PSV/PS4 来说，初始化时会加载系统设置，可能在那时已经交换过按键
            // 所以这里需要读取 isSwapInputKeys 的值，而不是直接设置为 true
            brls::Application::setSwapInputKeys(!brls::Application::isSwapInputKeys());
        }

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
#if defined(__PSV__) || defined(__PS4__)
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

        // Init keyboard shortcut
        brls::Application::getPlatform()->getInputManager()->getKeyboardKeyStateChanged()->subscribe(
            [this](brls::KeyState state) {
                if (!state.pressed) return;
                auto top = brls::Application::getActivitiesStack().back();
                switch (state.key) {
                case brls::BRLS_KBD_KEY_F:
                    // 在编辑框弹出时不触发
                    if (dynamic_cast<brls::EditTextDialog*>(top->getContentView())) break;
#ifndef __APPLE__
                case brls::BRLS_KBD_KEY_F11:
#endif
                    VideoContext::FULLSCREEN = !this->getItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    this->setItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    brls::Application::getPlatform()->getVideoContext()->fullScreen(VideoContext::FULLSCREEN);
                    break;
                case brls::BRLS_KBD_KEY_SPACE: {
                    MPVCore::instance().togglePlay();
                    VideoView* video = dynamic_cast<VideoView*>(top->getContentView()->getView("video"));
                    if (video) video->showOSD(true);
                    break;
                }
                default:;
                }
            });
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
#if defined(__PSV__) || defined(__PS4__)
        brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
#else
        std::string icon = getItem(KEYMAP, std::string("xbox"));
        if (icon == "xbox") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_xbox.ttf");
        } else if (icon == "ps") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
        } else if (brls::Application::isSwapInputKeys()) {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard_swap.ttf");
        } else {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
        }
#endif
    }

    brls::FontLoader::USER_EMOJI_PATH = configDir() + "/emoji.ttf";
    if (access(brls::FontLoader::USER_EMOJI_PATH.c_str(), F_OK) == -1) {
        // 自定义emoji不存在，使用内置emoji
        brls::FontLoader::USER_EMOJI_PATH = BRLS_ASSET("font/emoji.ttf");
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
    auto is_user = [this](const AppUser& u) { return u.id == this->user_id; };
    this->user = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (this->user == this->users.end()) return false;

    auto is_server = [this](const AppServer& s) { return s.id == this->user->server_id; };
    auto it = std::find_if(this->servers.begin(), this->servers.end(), is_server);
    if (it == this->servers.end()) return false;

    this->server_url = it->urls.front();
    HTTP::Header header = {this->getDevice(this->user->access_token)};
    std::string uri = this->server_url + jellyfin::apiInfo;
    try {
        std::string resp = HTTP::get(uri, header, HTTP::Timeout{});
        jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
        this->addServer(AppServer{
            .name = info.ServerName,
            .id = info.Id,
            .version = info.Version,
        });
        return true;
    } catch (const std::exception& ex) {
        brls::Logger::warning("AppConfig checkLogin: {}", ex.what());
        return false;
    }
}

bool AppConfig::checkDanmuku() {
    jellyfin::getJSON<jellyfin::PluginList>(
        [](const jellyfin::PluginList& plugins) {
            for (auto& p : plugins) {
                if (p.Name == "Danmu") {
                    DanmakuCore::PLUGIN_ACTIVE = true;
                    brls::Logger::info("Danmaku plugin found: {}", p.Version);
                    return;
                }
            }
            DanmakuCore::PLUGIN_ACTIVE = false;
            brls::Logger::info("Danmaku plugin not found");
        },
        [this](const std::string& err) {
            const std::string locale = brls::Application::getPlatform()->getLocale();
            bool enable = (locale == brls::LOCALE_ZH_HANS) || (locale == brls::LOCALE_ZH_HANT);
            DanmakuCore::PLUGIN_ACTIVE = this->getItem(DANMAKU, enable);
            brls::Logger::warning("checkDanmuku {} fallback ({})", err, DanmakuCore::PLUGIN_ACTIVE);
        },
        jellyfin::apiPlugins);
    return false;
}

std::string AppConfig::configDir() {
#if __SWITCH__
    return fmt::format("sdmc:/switch/{}", AppVersion::getPackageName());
#elif defined(__PS4__)
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
#if defined(__PS4__)
#elif __PSV__
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
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
    if (s.urls.size() > 0) {
        this->server_url = s.urls.front();
    }

    for (auto& o : this->servers) {
        if (s.id == o.id) {
            if (!s.name.empty()) o.name = s.name;
            if (!s.version.empty()) o.version = s.version;
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

void AppConfig::addUser(const AppUser& u, const std::string& url) {
    auto is_user = [u](const AppUser& o) { return o.id == u.id; };
    auto it = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (it != this->users.end()) {
        it->name = u.name;
        it->access_token = u.access_token;
        it->server_id = u.server_id;
    } else {
        it = this->users.insert(it, u);
    }
    this->server_url = url;
    this->user_id = u.id;
    this->user = it;
    this->save();
}

bool AppConfig::removeServer(const std::string& id) {
    for (auto it = this->servers.begin(); it != this->servers.end(); ++it) {
        if (it->id == id) {
            this->servers.erase(it);
            this->save();
            return this->servers.empty();
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

const std::vector<AppUser> AppConfig::getUsers(const std::string& id) const {
    std::vector<AppUser> users;
    for (auto& u : this->users) {
        if (u.server_id == id) {
            users.push_back(u);
        }
    }
    return users;
}