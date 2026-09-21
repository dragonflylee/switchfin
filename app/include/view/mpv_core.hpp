//
// Created by fang on 2022/8/12.
//

#pragma once

#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <mutex>
#include <string_view>
#endif
#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <mpv/client.h>
#include <utils/event.hpp>
#ifdef PS5_NATIVE_GPU
#include <utils/callback_gate.hpp>
#include <utils/ps5_native_mpv_notifications.hpp>
#include <utils/ps5_native_cache_budget.hpp>
#include <utils/observed_properties.hpp>
#include <utils/subtitle_appearance.hpp>
#endif
#ifdef MPV_SW_RENDER
#include <mpv/render.h>
#elif defined(BOREALIS_USE_D3D11)
#include <mpv/render_d3d11.h>
#elif defined(BOREALIS_USE_DEKO3D)
#include <mpv/render_dk3d.h>
#elif defined(BOREALIS_USE_GXM)
#include <mpv/render.h>
#include <mpv/render_gxm.h>
#include <nanovg_gxm_utils.h>
#else
#include <mpv/render_gl.h>
#if defined(__PSV__) || defined(__PS4__)
#include <GLES2/gl2.h>
#else
#include <glad/glad.h>
#endif
#endif

typedef std::unordered_map<std::string, std::string> MPVMap;

class MPVCore : public brls::Singleton<MPVCore> {
public:
    MPVCore();

    ~MPVCore() = default;

    void restart();

    void init();

    void clean();

#ifdef PS5_NATIVE_GPU
    // UI-thread-only; does not construct the singleton when no player exists.
    static void drainNativeCallbacks() noexcept;
    // Reconfigure audio output while preserving the player and its playlist.
    bool setNativeAudioChannels(const std::string& preference);

#endif
    template <typename... Args>
    void command(Args &&...args) {
        const char *cmd[] = {args..., nullptr};
#ifdef PS5_NATIVE_GPU
        if (!mpv || !cmd[0]) return;
        const std::string_view operation(cmd[0]);
        const bool mutatesPlaylist = operation == "stop" || operation == "loadfile" || operation == "loadlist" ||
            operation.rfind("playlist-", 0) == 0 ||
            (operation == "set" && cmd[1] && std::string_view(cmd[1]).rfind("playlist-pos", 0) == 0);
        std::unique_lock<std::mutex> playlistLock(playlistCommandMutex, std::defer_lock);
        if (mutatesPlaylist) {
            playlistLock.lock();
            ++playlistMutationGeneration;
        }
        mpv_command_async(mpv, 0, cmd);
#else
        if (mpv) mpv_command_async(mpv, 0, cmd);
#endif
    }

    bool isStopped() const;

    bool isPaused();
#ifdef PS5_NATIVE_GPU
    bool canSeek() const { return profileProperties.integer("seekable") != 0; }
#endif

    double getSpeed() const;

    std::string getCacheSpeed() const;

#ifdef PS5_NATIVE_GPU
    // Source dimensions choose the per-file decoder thread count (0 = unknown).
    int setUrl(const std::string &url, const std::string &extra = "", const std::string &method = "replace",
        uint64_t userdata = 0, int64_t sourceWidth = 0, int64_t sourceHeight = 0);

    // After append replies, select an index synchronously without loading I/O.
    int playPlaylistIndex(size_t index, int64_t expectedEntry, uint64_t expectedGeneration);
    uint64_t playlistGeneration() {
        std::lock_guard<std::mutex> lock(playlistCommandMutex);
        return playlistMutationGeneration;
    }
#else
    void setUrl(const std::string &url, const std::string &extra = "", const std::string &method = "replace",
        uint64_t userdata = 0);
#endif

    std::string getString(const std::string &key);

    double getDouble(const std::string &key);
    void setDouble(const std::string &key, double value);

    int64_t getInt(const std::string &key, int64_t default_value = 0);
    void setInt(const std::string &key, int64_t value);

#ifdef PS5_NATIVE_GPU
    void applySubtitlePreferences();

    // UI-only snapshot; never waits for mpv or exposes mpv-owned node storage.
    const ObservedProperties& getProfileProperties() const { return profileProperties; }
#else
    std::unordered_map<std::string, mpv_node> getNodeMap(const std::string &key);
#endif

    void togglePlay();

    void stop();

    void seek(double value, const std::string &flags = "absolute");

    void setSpeed(double value);

    // 是否开启视频渲染
    void enableVO(bool value);

    /**
     * 强制设置视频比例
     * @param value auto 为自动, 可设置 16:9 或 1.333 这两种形式的字符串
     */
    void setAspect(const std::string &value);

    void setFrameSize(brls::Rect rect);

    bool isValid();

    void draw(brls::Rect rect, float alpha = 1.0);

    /// @brief 播放器内部事件
    /// @return
    MPVEvent *getEvent() { return &this->mpvCoreEvent; }
#ifdef PS5_NATIVE_GPU

    struct PlaybackEvent {
        int64_t entry = -1;
        bool started = false;
        bool restarted = false;
    };
    // Valid only in the current event dispatch. Reentrant clean/init invalidates
    // the old dispatch owner instead of exposing the replacement player's state.
    PlaybackEvent playbackEvent() const {
        return callbacks && callbacks->gate.get() == playbackDispatchOwner && callbacks->gate->isActive()
            ? playbackDispatchState : PlaybackEvent{};
    }
#endif

    /// @brief 可以用于共享自定义事件
    /// @return
    MPVCustomEvent *getCustomEvent() { return &this->mpvCoreCustomEvent; }

    /// @brief 异步命令回调
    /// @return
    MPVCommandReply *getCommandReply() { return &this->mpvCommandReply; }

    void reset();

    MPVMap supportCodecs();

    // core states
    int64_t duration = 0;  // second
    int64_t video_progress = 0;
    int64_t volume = 0;
    double video_speed = 0;
    double playback_time = 0;

    inline static bool DEBUG = false;

    // Bottom progress bar
    inline static bool BOTTOM_BAR = true;
    inline static bool OSD_ON_TOGGLE = true;
    // 是否开启 TV 客户端的控制逻辑
    inline static bool OSD_TV_MODE = false;
    inline static bool TOUCH_GESTURE = true;
    inline static bool CLIP_POINT = true;

    // 低画质解码，剔除解码过程中的部分步骤，可以用来节省cpu
    inline static bool LOW_QUALITY = false;
    inline static bool SUBS_FALLBACK = true;

    // 视频缓存（是否使用内存缓存视频，值为缓存的大小，单位MB）
    inline static int INMEMORY_CACHE = 0;

    // 硬件解码
    inline static bool HARDWARE_DEC = false;
#ifdef PS5_NATIVE_GPU
#if defined(__SWITCH__) || defined(BOREALIS_USE_GXM) || defined(ANDROID)
    inline static std::string PLAYER_HWDEC_METHOD = "auto";
#elif defined(__PSV__)
    inline static std::string PLAYER_HWDEC_METHOD = "vita-copy";
#else
    // This FFmpeg/mpv build has no integrated PlayStation hardware decoder.
    inline static std::string PLAYER_HWDEC_METHOD = "no";
#endif
#else
#if defined(__SWITCH__) || defined(BOREALIS_USE_GXM) || defined(ANDROID)
    inline static std::string PLAYER_HWDEC_METHOD = "auto";
#elif defined(__PSV__)
    inline static std::string PLAYER_HWDEC_METHOD = "vita-copy";
#elif defined(__PS4__)
    inline static std::string PLAYER_HWDEC_METHOD = "no";
#else
    inline static std::string PLAYER_HWDEC_METHOD = "auto-safe";
#endif
#endif
#if defined(ANDROID)
    inline static std::string VO = "gpu";
#else
    inline static std::string VO = "libmpv";
#endif
    inline static std::string VIDEO_CODEC = "h264";
    inline static int64_t VIDEO_QUALITY = 0;

    inline static bool FORCE_DIRECTPLAY = false;

    // 触发倍速时的默认值，单位为 %
    inline static int VIDEO_SPEED = 200;

    // 是否镜像视频
    inline static int VIDEO_FILTER = 0;
    inline static int VIDEO_ROTATION = 0;
    // 强制的视频比例 (-1 为自动)
    inline static std::string VIDEO_ASPECT = "auto";

    inline static std::string AUDIO_CHANNELS = "auto-safe";

private:
#ifdef PS5_NATIVE_GPU
    std::mutex playlistCommandMutex;
    uint64_t playlistMutationGeneration = 0;
    PlaybackEvent playbackState, playbackDispatchState;
    const CallbackGate* playbackDispatchOwner = nullptr;
    subtitle_appearance::Preferences subtitlePreferences;
    struct CallbackContext {
        MPVCore* owner;
        std::shared_ptr<CallbackGate> gate = std::make_shared<CallbackGate>();
        Ps5NativeMpvNotifications notifications;
    };
    inline static MPVCore* nativeCallbackOwner = nullptr; // UI-thread-only singleton registration
    std::unique_ptr<CallbackContext> callbacks;
    bool subscriptionsActive = false;
    bool presentationPending = false;
    ObservedProperties profileProperties;
    uint64_t profileObservationId = 0;
    void observeProfileProperties();
    void clearProfileProperties();
#endif
    mpv_handle *mpv = nullptr;
    mpv_render_context *mpv_context = nullptr;
    brls::Rect rect = {0, 0, 1920, 1080};
    bool video_stopped = true;
    bool video_paused = false;
    int64_t cache_speed = 0;  // Bps

#ifdef MPV_SW_RENDER
    const int PIXCEL_SIZE = 4;
    int nvg_image = 0;
    const char *sw_format = "rgba";
    int sw_size[2] = {
        (int)brls::Application::windowWidth,
        (int)brls::Application::windowHeight,
    };
    size_t pitch = PIXCEL_SIZE * sw_size[0];
    void *pixels = nullptr;
#ifdef PS5_NATIVE_GPU
    // Set when mpv has actually rendered a new frame into `pixels`, cleared once
    // draw() has uploaded it. Without this the UI re-uploads the whole surface
    // every iteration -- 8.3 MB of glTexSubImage2D at 1080p -- even while paused
    // or when the UI is simply running faster than the video's frame rate.
    std::atomic<bool> sw_dirty{true};
#endif
    mpv_render_param mpv_params[5] = {
        {MPV_RENDER_PARAM_SW_SIZE, &sw_size[0]},
        {MPV_RENDER_PARAM_SW_FORMAT, (void *)sw_format},
        {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
        {MPV_RENDER_PARAM_SW_POINTER, pixels},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_D3D11)
    mpv_d3d11_fbo mpv_fbo;
    mpv_render_param mpv_params[2] = {
        {MPV_RENDER_PARAM_D3D11_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_DEKO3D)
    DkFence doneFence;
    DkFence readyFence;
    mpv_deko3d_fbo mpv_fbo{
        .tex = nullptr,
        .ready_fence = &readyFence,
        .done_fence = &doneFence,
        .w = 1280,
        .h = 720,
        .format = DkImageFormat_RGBA8_Unorm,
    };
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_DEKO3D_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_GXM)
    int nvg_image = 0;
    mpv_gxm_fbo mpv_fbo = {
        .render_target = nullptr,
        .color_surface = nullptr,
        .depth_stencil_surface = nullptr,
        .w = DISPLAY_WIDTH,
        .h = DISPLAY_HEIGHT,
        .format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_RGBA,
    };
    int flip_y{1};
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_GXM_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#else
    GLint default_framebuffer = 0;
#ifdef PS5_NATIVE_GPU
    mpv_opengl_fbo mpv_fbo{};
#else
    mpv_opengl_fbo mpv_fbo;
#endif
    int flip_y{1};
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#endif

    // MPV 内部事件，传递内容为: 事件类型
    MPVEvent mpvCoreEvent;
    // 自定义的事件，传递内容为: string类型的事件名与一个任意类型的指针
    MPVCustomEvent mpvCoreCustomEvent;
    // 命令异步回调
    MPVCommandReply mpvCommandReply;

    // 当前软件是否在前台的回调
    brls::Event<bool>::Subscription focusSubscription;

    // window/framebuffer size changes (Switch dock/undock): the mpv render
    // target size is in pixels and must follow Application::windowWidth/Height
    brls::VoidEvent::Subscription sizeSubscription;

    /// Will be called in main thread to get events from mpv core
    void eventMainLoop();

#ifdef PS5_NATIVE_GPU
    int64_t nativeDecoderThreads = 2;
    ps5_native_cache_budget::Limits nativeCacheLimits;

    static void on_update(void *self) noexcept;
    static void on_wakeup(void *self) noexcept;
#else
    static void on_update(void *self);
    static void on_wakeup(void *self);
#endif
};
