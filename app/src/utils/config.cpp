#ifdef __SWITCH__
#include <switch.h>
#include "utils/overclock.hpp"
#elif defined(__PSV__)
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/appmgr.h>
#include <psp2/vshbridge.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>

extern "C" {
unsigned int _newlib_heap_size_user = 220 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;
}
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
#elif defined(ANDROID)
#include <SDL2/SDL_system.h>
#include <jni.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#if defined(_WIN32)
#include <shlobj.h>
#endif

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;
#endif

#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include <borealis/views/edit_text_dialog.hpp>
#include "api/jellyfin.hpp"
#include "utils/config.hpp"
#include "utils/keybind.hpp"
#include "utils/misc.hpp"
#include "utils/ums.hpp"
#include "utils/thread.hpp"
#include "view/mpv_core.hpp"
#include "view/danmaku_core.hpp"
#include "view/video_view.hpp"

std::unordered_map<AppConfig::Item, AppConfig::Option> AppConfig::settingMap = {
    {APP_THEME, {"app_theme", {"auto", "light", "dark"}}},
    {APP_LANG, {"app_lang", {brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_ZH_HANS, brls::LOCALE_ZH_HANT,
                                brls::LOCALE_JA, brls::LOCALE_Ko, brls::LOCALE_RU, brls::LOCALE_DE, brls::LOCALE_FR,
                                brls::LOCALE_ES, brls::LOCALE_PT, "cs", "uk", "tr", "vi"}}},
    {APP_UPDATE, {"app_update"}},
    {APP_UI_SCALE, {"app_ui_scale", {"544p", "720p", "900p", "1080p"}}},
    {AUDIO_CHANNELS, {"audio-channels", {"auto-safe", "stereo", "mono"}}},
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
    {MPV_VO, {"mpv_vo", {"gpu", "gpu-next", "mediacodec_embed"}}},
    {PLAYER_BOTTOM_BAR, {"player_bottom_bar"}},
    {PLAYER_LOW_QUALITY, {"player_low_quality"}},
    {PLAYER_SUBS_FALLBACK, {"player_subs_fallback"}},
    {PLAYER_INMEMORY_CACHE,
        {
            "player_inmemory_cache",
            {"0MB", "10MB", "20MB", "50MB", "100MB", "200MB", "500MB"},
            {0, 10, 20, 50, 100, 200, 500},
        }},
    {PLAYER_SPEED,
        {
            "player_speed",
            {"4x", "3x", "2x"},
            {400, 300, 200},
        }},
    {PLAYER_HWDEC, {"player_hwdec"}},
    {PLAYER_HWDEC_CUSTOM, {"player_hwdec_custom"}},
    {PLAYER_ASPECT, {"player_aspect", {"auto", "stretch", "crop", "4:3", "16:9"}}},
    {PLAYER_TV_MODE, {"player_tv_mode"}},
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
    {SHOW_FPS, {"show_fps"}},
    {SWAP_INTERVAL, {"swap_interval"}},
    {APP_SWAP_ABXY, {"app_swap_abxy"}},
    {TEXTURE_CACHE_NUM, {"texture_cache_num"}},
    {REQUEST_THREADS, {"request_threads", {"1", "2", "4", "8"}, {1, 2, 4, 8}}},
    {REQUEST_TIMEOUT,
        {"request_timeout", {"3000", "5000", "10000", "20000", "30000"}, {3000, 5000, 10000, 20000, 30000}}},
    {HTTP_PROXY_STATUS, {"http_proxy_status"}},
    {HTTP_PROXY, {"http_proxy"}},

    {DOWNLOAD_QUALITY, {"download_quality", {"Original", "1080p", "720p", "480p"}, {0, 1, 2, 3}}},

    {KEY_REFRESH, {"key_refresh"}},
    {KEY_LAST, {"key_last"}},
    {KEY_NEXT, {"key_next"}},
    {KEY_VOLUME_UP, {"key_volume_up"}},
    {KEY_VOLUME_DOWN, {"key_volume_down"}},
    {KEY_VIDEO_PROFILE, {"key_video_profile"}},
    {KEY_DANMAKU, {"key_danmaku"}},
    {KEY_FORWARD, {"key_forward"}},
    {KEY_REWIND, {"key_rewind"}},
    {KEY_SETTING, {"key_setting"}},
    {KEY_VIDEO_QUALITY, {"key_video_quality"}},
    {KEY_VIDEO_SPEED, {"key_video_speed"}},
    {KEY_VIDEO_OSD, {"key_video_osd"}},
    {KEY_VIDEO_PAUSE, {"key_video_pause"}},
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
#elif defined(__PSV__)
    char cid[0x20];
    if (_vshSblAimgrGetConsoleId(cid) >= 0) {
        char text[0x40];
        sceClibSnprintf(text, sizeof(text) - 1, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            cid[0x0], cid[0x1], cid[0x2], cid[0x3], cid[0x4], cid[0x5], cid[0x6], cid[0x7], cid[0x8], cid[0x9],
            cid[0xA], cid[0xB], cid[0xC], cid[0xD], cid[0xE], cid[0xF]);
        return text;
    }
#elif defined(ANDROID)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
    if (utilsClass) {
        jmethodID jmethod = env->GetStaticMethodID(utilsClass, "getAndroidId", "()Ljava/lang/String;");
        jstring jname = (jstring)env->CallStaticObjectMethod(utilsClass, jmethod);
        const char* name = env->GetStringUTFChars(jname, nullptr);
        std::string deviceId = name;
        env->ReleaseStringUTFChars(jname, name);
        env->DeleteLocalRef(jname);
        env->DeleteLocalRef(utilsClass);
        return deviceId;
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
#elif defined(__linux__)
    static const std::vector<std::string> dev_names = {
        "/sys/devices/virtual/dmi/id/board_serial",
        "/proc/device-tree/serial-number",
        "/etc/machine-id",
    };
    for (auto& path : dev_names) {
        std::ifstream f(path.c_str());
        if (f.is_open()) {
            std::string name;
            std::getline(f, name);
            if (name.size() > 0) {
                return misc::hexEncode((uint8_t*)name.data(), name.size());
            }
        }
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

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID) && !defined(TRIMUI)
    brls::DesktopPlatform::GAMEPAD_DB = configDir() + "/gamecontrollerdb.txt";
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
#elif defined(__PSV__)
    int search_unk[2];
    if (_vshKernelSearchModuleByName("CapUnlocker", search_unk) >= 0) {
        brls::sync([]() { brls::Application::notify("CapUnlocker found"); });
        sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_SYSTEM);
    }
#elif defined(__PS4__)
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0) brls::Logger::error("cannot load net module");
    primary_dns = inet_addr("223.5.5.5");
    secondary_dns = inet_addr("1.1.1.1");
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders = 0;
    // 在加载第一帧之后隐藏启动画面
    brls::sync([]() { sceSystemServiceHideSplashScreen(); });
#endif

    std::string uiScale = this->getItem(APP_UI_SCALE, std::string(""));
    if (uiScale == "544p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 960;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 544;
    } else if (uiScale == "720p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1280;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 720;
    } else if (uiScale == "900p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1600;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 900;
    } else if (uiScale == "1080p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH = 1920;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 1080;
    }

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
    MPVCore::VO = this->getItem(MPV_VO, MPVCore::VO);
    MPVCore::HARDWARE_DEC = this->getItem(PLAYER_HWDEC, true);
    MPVCore::FORCE_DIRECTPLAY = this->getItem(FORCE_DIRECTPLAY, false);
    MPVCore::VIDEO_CODEC = this->getItem(TRANSCODEC, MPVCore::VIDEO_CODEC);
    MPVCore::AUDIO_CHANNELS = this->getItem(AUDIO_CHANNELS, MPVCore::AUDIO_CHANNELS);
    // 初始化自定义的硬件加速方案
    MPVCore::PLAYER_HWDEC_METHOD = this->getItem(PLAYER_HWDEC_CUSTOM, MPVCore::PLAYER_HWDEC_METHOD);
    // 初始化默认的倍速设定
    MPVCore::VIDEO_SPEED = this->getItem(PLAYER_SPEED, MPVCore::VIDEO_SPEED);
    // 初始化视频比例
    MPVCore::VIDEO_ASPECT = this->getItem(PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);
    MPVCore::OSD_TV_MODE = this->getItem(PLAYER_TV_MODE, false);

    // 初始化弹幕相关内容
    DanmakuCore::DANMAKU_ON = this->getItem(DANMAKU_ON, true);
    DanmakuCore::DANMAKU_STYLE_AREA = this->getItem(DANMAKU_STYLE_AREA, 100);
    DanmakuCore::DANMAKU_STYLE_ALPHA = this->getItem(DANMAKU_STYLE_ALPHA, 80);
    DanmakuCore::DANMAKU_STYLE_FONTSIZE = this->getItem(DANMAKU_STYLE_FONTSIZE, 30);
    DanmakuCore::DANMAKU_STYLE_LINE_HEIGHT = this->getItem(DANMAKU_STYLE_LINE_HEIGHT, 120);
    DanmakuCore::DANMAKU_STYLE_SPEED = this->getItem(DANMAKU_STYLE_SPEED, 100);

    ThreadPool::max_thread_num = this->getItem(REQUEST_THREADS, ThreadPool::max_thread_num);

    // 初始化 deviceId
    if (this->device.empty()) this->device = generateDeviceId();

    // 初始化i18n
    brls::Platform::APP_LOCALE_DEFAULT = this->getItem(APP_LANG, brls::LOCALE_AUTO);

    brls::Application::setFPSStatus(this->getItem(SHOW_FPS, false));
    VideoContext::swapInterval = this->getItem(SWAP_INTERVAL, 1);

    // 初始化 KeyBind
    KeyBind::setLast(this->getItem(KEY_LAST, std::string{"pgup"}));
    KeyBind::setNext(this->getItem(KEY_NEXT, std::string{"pgdn"}));
    KeyBind::setVolumeUp(this->getItem(KEY_VOLUME_UP, std::string{"0"}));
    KeyBind::setVolumeDown(this->getItem(KEY_VOLUME_DOWN, std::string{"9"}));
    KeyBind::setDanmaku(this->getItem(KEY_DANMAKU, std::string{"d"}));
    KeyBind::setVideoProfile(this->getItem(KEY_VIDEO_PROFILE, std::string{"f1"}));
    KeyBind::setVideoQuality(this->getItem(KEY_VIDEO_QUALITY, std::string{"f2"}));
    KeyBind::setVideoSpeed(this->getItem(KEY_VIDEO_SPEED, std::string{"f3"}));
    KeyBind::setSetting(this->getItem(KEY_SETTING, std::string{"f4"}));
    KeyBind::setRefresh(this->getItem(KEY_REFRESH, std::string{"f5"}));
    KeyBind::setForward(this->getItem(KEY_FORWARD, std::string{"]"}));
    KeyBind::setRewind(this->getItem(KEY_REWIND, std::string{"["}));
    KeyBind::setVideoOsd(this->getItem(KEY_VIDEO_OSD, std::string{"o"}));
    KeyBind::setVideoPause(this->getItem(KEY_VIDEO_PAUSE, std::string{"space"}));

    // 初始化一些在创建窗口之后才能初始化的内容
    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
#if defined(TRIMUI)
        if (this->getItem(APP_SWAP_ABXY, true))
#else
        if (this->getItem(APP_SWAP_ABXY, false))
#endif
        {
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

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID)
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
                switch (state.key) {
#ifndef __APPLE__
                case brls::BRLS_KBD_KEY_F11:
                    VideoContext::FULLSCREEN = !this->getItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    this->setItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
                    brls::Application::getPlatform()->getVideoContext()->fullScreen(VideoContext::FULLSCREEN);
                    break;
#endif
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
    Ums::instance().init();

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
    for (auto& s : this->servers) {
        if (s.id.empty() && s.urls.size() > 0) {
            try {
                std::string url = s.urls.front() + jellyfin::apiPublicInfo;
                std::string resp = HTTP::get(url, HTTP::Timeout{3000});
                jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
                s.id = info.Id;
                s.name = info.ServerName;
            } catch (const std::exception& ex) {
                brls::Logger::warning("AppConfig {} checkServer: {}", s.urls.front(), ex.what());
                return false;
            }
        }
    }

    auto is_user = [this](const AppUser& u) { return u.id == this->user_id; };
    this->user = std::find_if(this->users.begin(), this->users.end(), is_user);
    if (this->user == this->users.end()) return false;

    auto is_server = [this](const AppServer& s) { return s.id == this->user->server_id; };
    auto it = std::find_if(this->servers.begin(), this->servers.end(), is_server);
    if (it == this->servers.end()) return false;

    this->server_url = it->urls.front();
    HTTP::Header header = {this->getAuth(this->user->access_token)};
    std::string uri = fmt::format("{}/Users/{}", this->server_url, this->user_id);
    try {
        std::string resp = HTTP::get(uri, header, HTTP::Timeout{});
        jellyfin::UserInfo info = nlohmann::json::parse(resp);
        this->user->is_admin = info.Policy.IsAdministrator;
        this->user->config = std::move(info.Configuration);
        return true;
    } catch (const std::exception& ex) {
        brls::Logger::warning("AppConfig {} checkLogin: {}", this->server_url, ex.what());
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
#elif defined(ANDROID)
    return SDL_AndroidGetExternalStoragePath();
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
#if !defined(__PS4__) && !defined(__SWITCH__) && !defined(ANDROID)
    if (brls::DesktopPlatform::RESTART_APP) {
        brls::Logger::info("Restart app {}", argv[0]);

#if defined(__PSV__)
        sceAppMgrLoadExec(argv[0], argv, nullptr);
#else
        execv(argv[0], argv);
#endif
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
        it->is_admin = u.is_admin;
        it->config = std::move(u.config);
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

void AppConfig::addRemote(const AppRemote& r) {
    this->remotes.push_back(r);
    this->save();
}

void AppConfig::updateRemote(size_t index, const AppRemote& r) {
    if (index >= this->remotes.size()) return;
    this->remotes[index] = r;
    this->save();
}

void AppConfig::removeRemote(size_t index) {
    if (index >= this->remotes.size()) return;
    this->remotes.erase(this->remotes.begin() + index);
    this->save();
}

std::string AppConfig::getAuth(const std::string& token) {
    if (this->device_name.empty()) this->device_name = AppVersion::getDeviceName();

    if (token.empty())
        return fmt::format("Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
            AppVersion::getPackageName(), this->device_name, this->device, AppVersion::getVersion());
    else
        return fmt::format(
            "Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\", "
            "Token=\"{}\"",
            AppVersion::getPackageName(), this->device_name, this->device, AppVersion::getVersion(), token);
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

void AppConfig::addColor(const brls::ThemeVariant tv, const std::string& name, NVGcolor defaultColor) {
    auto& theme = (tv == brls::ThemeVariant::LIGHT) ? brls::Theme::getLightTheme() : brls::Theme::getDarkTheme();

    if (!setting.contains(name)) {
        theme.addColor(name, defaultColor);
    } else {
        unsigned int r = 0, g = 0, b = 0;
        std::string s = setting.at(name).get<std::string>();

        std::stringstream sr{s.substr(1, 2)};
        sr >> std::hex >> r;
        std::stringstream sg{s.substr(3, 2)};
        sg >> std::hex >> g;
        std::stringstream sb{s.substr(5, 2)};
        sb >> std::hex >> b;
        theme.addColor(name, nvgRGB(r, g, b));
    }
}

void AppConfig::initThemes() {
    this->addColor(brls::ThemeVariant::LIGHT, "color/app", nvgRGB(2, 176, 183));
    this->addColor(brls::ThemeVariant::DARK, "color/app", nvgRGB(51, 186, 227));
    // metadata pills (detail pages)
    this->addColor(brls::ThemeVariant::LIGHT, "color/pill", nvgRGBA(0, 0, 0, 18));
    this->addColor(brls::ThemeVariant::DARK, "color/pill", nvgRGBA(255, 255, 255, 22));
    // surfaces placed over the background (content cards, PIN code panel...)
    this->addColor(brls::ThemeVariant::LIGHT, "color/surface", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/surface", nvgRGB(22, 24, 29));
    // 用于骨架屏背景色
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_1", nvgRGB(245, 246, 247));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_1", nvgRGB(51, 52, 53));
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_2", nvgRGB(245, 245, 245));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_2", nvgRGB(51, 53, 55));
    this->addColor(brls::ThemeVariant::LIGHT, "color/grey_3", nvgRGBA(200, 200, 200, 16));
    this->addColor(brls::ThemeVariant::DARK, "color/grey_3", nvgRGBA(160, 160, 160, 160));
    this->addColor(brls::ThemeVariant::LIGHT, "color/danger", nvgRGB(198, 28, 28));
    this->addColor(brls::ThemeVariant::DARK, "color/danger", nvgRGBA(198, 28, 28, 180));
    this->addColor(brls::ThemeVariant::LIGHT, "color/white", nvgRGB(255, 255, 255));
    this->addColor(brls::ThemeVariant::DARK, "color/white", nvgRGBA(255, 255, 255, 180));
    // 分割线颜色
    this->addColor(brls::ThemeVariant::LIGHT, "color/line", nvgRGB(208, 208, 208));
    this->addColor(brls::ThemeVariant::DARK, "color/line", nvgRGB(100, 100, 100));
    // 深浅配色通用的灰色字体颜色
    this->addColor(brls::ThemeVariant::LIGHT, "font/grey", nvgRGB(148, 153, 160));
    this->addColor(brls::ThemeVariant::DARK, "font/grey", nvgRGB(148, 153, 160));

    if (brls::Application::ORIGINAL_WINDOW_HEIGHT == 544) {
        brls::getStyle().addMetric("app/album/height", 215);
        brls::getStyle().addMetric("app/books/height", 270);
        brls::getStyle().addMetric("app/video/height", 290);
        // row = width x image ratio (poster 2:3 = 1.5, wide 16:9 = 0.5625)
        // + 55 of label area (margin 10 + title 25 + subtitle 20), so the
        // image fill keeps exactly the media's ratio
        brls::getStyle().addMetric("app/card/poster/width", 150);
        brls::getStyle().addMetric("app/card/poster/row", 280);
        brls::getStyle().addMetric("app/card/wide/width", 280);
        brls::getStyle().addMetric("app/card/wide/row", 213);
        brls::getStyle().addMetric("app/grid/6", 5);
        brls::getStyle().addMetric("app/grid/5", 4);
        brls::getStyle().addMetric("app/grid/4", 3);
        brls::getStyle().addMetric("app/grid/3", 2);
        brls::getStyle().addMetric("app/grid/2", 1);
        brls::getStyle().addMetric("brls/tab_frame/content_padding_sides", 30);
        brls::getStyle().addMetric("main/content_padding_sides", 15);
        brls::getStyle().addMetric("main/content_padding_top_bottom", 20);
    } else {
        switch (brls::Application::ORIGINAL_WINDOW_HEIGHT) {
        case 1080:
            brls::getStyle().addMetric("app/album/height", 250);
            brls::getStyle().addMetric("app/books/height", 320);
            brls::getStyle().addMetric("app/video/height", 340);
            brls::getStyle().addMetric("app/card/poster/width", 225);
            brls::getStyle().addMetric("app/card/poster/row", 393);
            brls::getStyle().addMetric("app/card/wide/width", 410);
            brls::getStyle().addMetric("app/card/wide/row", 286);
            brls::getStyle().addMetric("app/grid/6", 8);
            brls::getStyle().addMetric("app/grid/5", 7);
            brls::getStyle().addMetric("app/grid/4", 6);
            brls::getStyle().addMetric("app/grid/3", 5);
            brls::getStyle().addMetric("app/grid/2", 4);
            break;
        case 900:
            brls::getStyle().addMetric("app/album/height", 240);
            brls::getStyle().addMetric("app/books/height", 305);
            brls::getStyle().addMetric("app/video/height", 325);
            brls::getStyle().addMetric("app/card/poster/width", 205);
            brls::getStyle().addMetric("app/card/poster/row", 363);
            brls::getStyle().addMetric("app/card/wide/width", 375);
            brls::getStyle().addMetric("app/card/wide/row", 266);
            brls::getStyle().addMetric("app/grid/6", 7);
            brls::getStyle().addMetric("app/grid/5", 6);
            brls::getStyle().addMetric("app/grid/4", 5);
            brls::getStyle().addMetric("app/grid/3", 4);
            brls::getStyle().addMetric("app/grid/2", 3);
            break;
        default:
            brls::getStyle().addMetric("app/album/height", 225);
            brls::getStyle().addMetric("app/books/height", 280);
            brls::getStyle().addMetric("app/video/height", 300);
            // row = width x image ratio + 55 of labels (cf. PSV block)
            brls::getStyle().addMetric("app/card/poster/width", 185);
            brls::getStyle().addMetric("app/card/poster/row", 333);
            brls::getStyle().addMetric("app/card/wide/width", 340);
            brls::getStyle().addMetric("app/card/wide/row", 246);
            brls::getStyle().addMetric("app/grid/6", 6);
            brls::getStyle().addMetric("app/grid/5", 5);
            brls::getStyle().addMetric("app/grid/4", 4);
            brls::getStyle().addMetric("app/grid/3", 3);
            brls::getStyle().addMetric("app/grid/2", 2);
        }
        brls::getStyle().addMetric("main/content_padding_sides", 25);
        brls::getStyle().addMetric("main/content_padding_top_bottom", 30);
    }
}