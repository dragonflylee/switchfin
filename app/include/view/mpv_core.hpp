//
// Created by fang on 2022/8/12.
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <mpv/client.h>
#ifdef MPV_SW_RENDER
#include <mpv/render.h>
#elif defined(BOREALIS_USE_D3D11)
#include <mpv/render_dxgi.h>
#elif defined(BOREALIS_USE_DEKO3D)
#include <mpv/render_dk3d.h>
#else
#include <mpv/render_gl.h>
#endif

typedef enum MpvEventEnum {
    MPV_LOADED,
    MPV_PAUSE,
    MPV_RESUME,
    MPV_STOP,
    LOADING_START,
    LOADING_END,
    UPDATE_DURATION,
    UPDATE_PROGRESS,
    START_FILE,
    END_OF_FILE,
    CACHE_SPEED_CHANGE,
    VIDEO_SPEED_CHANGE,
    VIDEO_VOLUME_CHANGE,
    VIDEO_MUTE,
    VIDEO_UNMUTE,
    MPV_FILE_ERROR,
    RESET,
} MpvEventEnum;

typedef brls::Event<MpvEventEnum> MPVEvent;
typedef brls::Event<std::string, void *> MPVCustomEvent;
typedef std::unordered_map<std::string, std::string> MPVMap;
typedef brls::Event<uint64_t, int64_t> MPVCommandReply;

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

    void setShader(const std::string &profile, const std::string &shaders, bool showHint = true);

    void clearShader(bool showHint = true);

    void showOsdText(const std::string &value, int duration = 2000);

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
    inline static bool TOUCH_GESTURE = true;
    inline static bool CLIP_POINT = true;

    // 低画质解码，剔除解码过程中的部分步骤，可以用来节省cpu
    inline static bool LOW_QUALITY = false;

    // 视频缓存（是否使用内存缓存视频，值为缓存的大小，单位MB）
    inline static int INMEMORY_CACHE = 0;

    // 硬件解码
    inline static bool HARDWARE_DEC = false;
    inline static std::string PLAYER_HWDEC_METHOD = "auto";
    inline static std::string VIDEO_CODEC = "h264";
    inline static int64_t VIDEO_QUALITY = 0;

    inline static bool FORCE_DIRECTPLAY = false;

    // 触发倍速时的默认值，单位为 %
    inline static int VIDEO_SPEED = 200;
    inline static int SEEKING_STEP = 15;

    // 是否镜像视频
    inline static int VIDEO_FILTER = 0;
    // 强制的视频比例 (-1 为自动)
    inline static std::string VIDEO_ASPECT = "auto";

    NVGcolor bottomBarColor = brls::Application::getTheme().getColor("color/app");

private:
    mpv_handle *mpv = nullptr;
    mpv_render_context *mpv_context = nullptr;
    bool video_stopped = true;
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
    mpv_deko3d_fbo mpv_fbo{nullptr, &readyFence, &doneFence, 1280, 720, DkImageFormat_RGBA8_Unorm};
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_DEKO3D_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#else
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

    /// Will be called in main thread to get events from mpv core
    void eventMainLoop();

    static void on_update(void *self);
    static void on_wakeup(void *self);
};
