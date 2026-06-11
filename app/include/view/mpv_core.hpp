//
// Created by fang on 2022/8/12.
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <mpv/client.h>
#include <utils/event.hpp>
#ifdef MPV_SW_RENDER
#include <mpv/render.h>
#elif defined(BOREALIS_USE_D3D11)
#include <mpv/render_dxgi.h>
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

    template <typename... Args>
    void command(Args &&...args) {
        const char *cmd[] = {args..., nullptr};
        if (mpv) mpv_command_async(mpv, 0, cmd);
    }

    bool isStopped() const;

    bool isPaused();

    double getSpeed() const;

    std::string getCacheSpeed() const;

    void setUrl(const std::string &url, const std::string &extra = "", const std::string &method = "replace",
        uint64_t userdata = 0);

    std::string getString(const std::string &key);

    double getDouble(const std::string &key);
    void setDouble(const std::string &key, double value);

    int64_t getInt(const std::string &key, int64_t default_value = 0);
    void setInt(const std::string &key, int64_t value);

    std::unordered_map<std::string, mpv_node> getNodeMap(const std::string &key);

    void togglePlay();

    void stop();

    void seek(int64_t value, const std::string &flags = "absolute");

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
#if defined(__SWITCH__) || defined(BOREALIS_USE_GXM) || defined(ANDROID)
    inline static std::string PLAYER_HWDEC_METHOD = "auto";
#elif defined(__PSV__)
    inline static std::string PLAYER_HWDEC_METHOD = "vita-copy";
#elif defined(__PS4__)
    inline static std::string PLAYER_HWDEC_METHOD = "no";
#else
    inline static std::string PLAYER_HWDEC_METHOD = "auto-safe";
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
    mpv_render_param mpv_params[5] = {
        {MPV_RENDER_PARAM_SW_SIZE, &sw_size[0]},
        {MPV_RENDER_PARAM_SW_FORMAT, (void *)sw_format},
        {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
        {MPV_RENDER_PARAM_SW_POINTER, pixels},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_D3D11)
    mpv_render_param mpv_params[1] = {
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
    mpv_opengl_fbo mpv_fbo;
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

    static void on_update(void *self);
    static void on_wakeup(void *self);
};
