//
// Created by fang on 2022/8/12.
//

#include "view/mpv_core.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include <fmt/ranges.h>

#ifdef __PS4__
extern "C" {
extern int ps4_mpv_use_precompiled_shaders;
extern int ps4_mpv_dump_shaders;
}
#endif

static inline void check_error(int status) {
    if (status < 0) brls::Logger::error("MPV ERROR => {}", mpv_error_string(status));
}

#ifndef MPV_SW_RENDER
#ifdef BOREALIS_USE_D3D11
#include <borealis/platforms/driver/d3d11.hpp>
extern std::unique_ptr<brls::D3D11Context> D3D11_CONTEXT;
#elif defined(BOREALIS_USE_DEKO3D)
#include <borealis/platforms/switch/switch_video.hpp>
#else
#if defined(__PSV__) || defined(PS4)
#include <GLES2/gl2.h>
#else
#include <glad/glad.h>
#endif
#ifdef __SDL2__
#include <SDL2/SDL.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
static void *get_proc_address(void *unused, const char *name) {
#ifdef __SDL2__
    SDL_GL_GetCurrentContext();
    return (void *)SDL_GL_GetProcAddress(name);
#else
    glfwGetCurrentContext();
    return (void *)glfwGetProcAddress(name);
#endif
}
#endif
#endif

void MPVCore::on_update(void *self) {
    MPVCore *mpv = reinterpret_cast<MPVCore *>(self);
    brls::sync([mpv]() { mpv_render_context_update(mpv->mpv_context); });
}

void MPVCore::on_wakeup(void *self) {
    MPVCore *mpv = reinterpret_cast<MPVCore *>(self);
    brls::sync([mpv]() { mpv->eventMainLoop(); });
}

MPVCore::MPVCore() {
#ifdef __PS4_
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders = 0;
#endif
    this->init();
    // Destroy mpv when application exit
    brls::Application::getExitEvent()->subscribe([this]() {
        this->clean();
#ifdef MPV_SW_RENDER
        if (this->pixels) {
            free(this->pixels);
            this->pixels = nullptr;
            this->mpv_params[3].data = nullptr;
        }
#endif
    });
}

void MPVCore::init() {
    this->mpv = mpv_create();
    if (!mpv) {
        brls::fatal("Error Create mpv Handle");
    }

    auto &conf = AppConfig::instance();

    // misc
    mpv_set_option_string(mpv, "config", "yes");
    mpv_set_option_string(mpv, "config-dir", conf.configDir().c_str());
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "referrer", conf.getUrl().c_str());
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "video-timing-offset", "0");  // 60fps
    mpv_set_option_string(mpv, "reset-on-next-file", "speed,pause");
    mpv_set_option_string(mpv, "subs-fallback", SUBS_FALLBACK ? "yes" : "no");
    mpv_set_option_string(mpv, "vo", "libmpv");

    if (MPVCore::LOW_QUALITY) {
        // Less cpu cost
        brls::Logger::info("lavc: skip loop filter and set fast decode");
        mpv_set_option_string(mpv, "vd-lavc-skiploopfilter", "all");
        mpv_set_option_string(mpv, "vd-lavc-fast", "yes");
        if (mpv_client_api_version() > MPV_MAKE_VERSION(2, 1)) {
            mpv_set_option_string(mpv, "profile", "fast");
        }
    }

    if (MPVCore::INMEMORY_CACHE) {
        // cache
        brls::Logger::info("set memory cache: {}MB", MPVCore::INMEMORY_CACHE);
        mpv_set_option_string(mpv, "demuxer-max-bytes", fmt::format("{}MiB", MPVCore::INMEMORY_CACHE).c_str());
        mpv_set_option_string(mpv, "demuxer-max-back-bytes", fmt::format("{}MiB", MPVCore::INMEMORY_CACHE / 2).c_str());
    } else {
        mpv_set_option_string(mpv, "cache", "no");
    }
    // Making the loading process faster
#if defined(__SWITCH__)
    mpv_set_option_string(mpv, "vd-lavc-dr", "yes");
    mpv_set_option_string(mpv, "vd-lavc-threads", "3");

    // Set default subfont
    std::string locale = brls::Application::getPlatform()->getLocale();
    if (locale == brls::LOCALE_ZH_HANS)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_org_zh-cn_003");
    else if (locale == brls::LOCALE_ZH_HANT)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udjxh-db_zh-tw_003");
    else if (locale == brls::LOCALE_Ko)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_ko_003");
#elif defined(PS4)
    mpv_set_option_string(mpv, "vd-lavc-threads", "6");
#elif defined(__PSV__)
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    // Fix vo_wait_frame() cannot be wakeup
    mpv_set_option_string(mpv, "video-latency-hacks", "yes");
#endif

    // hardware decoding
    if (MPVCore::HARDWARE_DEC) {
#ifdef __SWITCH__
        mpv_set_option_string(mpv, "hwdec", "auto");
        brls::Logger::info("MPV hardware decode: {}", "auto");
#elif defined(__PSV__)
        mpv_set_option_string(mpv, "hwdec", "vita-copy");
        brls::Logger::info("MPV hardware decode: vita-copy");
#else
        mpv_set_option_string(mpv, "hwdec", PLAYER_HWDEC_METHOD.c_str());
        brls::Logger::info("MPV hardware decode: {}", PLAYER_HWDEC_METHOD);
#endif
    } else {
        mpv_set_option_string(mpv, "hwdec", "no");
    }

    if (MPVCore::DEBUG) {
        mpv_set_option_string(mpv, "terminal", "yes");
        //  mpv_set_option_string(mpv, "msg-level", "all=no");
        mpv_set_option_string(mpv, "msg-level", "all=v");
    } else if (brls::Application::isDebuggingViewEnabled()) {
        mpv_request_log_messages(mpv, "info");
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (conf.getItem(AppConfig::SINGLE, false)) {
        mpv_set_option_string(mpv, "input-ipc-server", conf.ipcSocket().c_str());
    }
#endif

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("Could not initialize mpv context");
    }

    this->setAspect(VIDEO_ASPECT);

    // set observe properties
    check_error(mpv_observe_property(mpv, 1, "core-idle", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 2, "pause", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 3, "duration", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 4, "playback-time", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 5, "cache-speed", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 9, "speed", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 10, "volume", MPV_FORMAT_INT64));

    // init renderer params
#ifdef MPV_SW_RENDER
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_D3D11)
    mpv_dxgi_init_params init_params{D3D11_CONTEXT->getDevice(), D3D11_CONTEXT->getSwapChain()};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_DXGI)},
        {MPV_RENDER_PARAM_DXGI_INIT_PARAMS, &init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_DEKO3D)
    auto videoContext = dynamic_cast<brls::SwitchVideoContext *>(brls::Application::getPlatform()->getVideoContext());
    mpv_deko3d_init_params deko_init_params{videoContext->getDeko3dDevice()};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_DEKO3D)},
        {MPV_RENDER_PARAM_DEKO3D_INIT_PARAMS, &deko_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#else
    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#endif

    if (mpv_render_context_create(&mpv_context, mpv, params) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("failed to initialize mpv context");
    }
#ifdef BOREALIS_USE_D3D11
    misc::initCrashDump();
#endif
    brls::Logger::info("version: {} ffmpeg {}", mpv_get_property_string(mpv, "mpv-version"),
        mpv_get_property_string(mpv, "ffmpeg-version"));

    this->command("set", "audio-client-name", AppVersion::getPackageName().c_str());
    // set event callback
    mpv_set_wakeup_callback(mpv, on_wakeup, this);
    // set render callback
    mpv_render_context_set_update_callback(mpv_context, on_update, this);

    focusSubscription = brls::Application::getWindowFocusChangedEvent()->subscribe([this](bool focus) {
        static bool playing = false;
        if (!focus) {
            // application is sleep, save the current state
            playing = !isPaused();
            command("set", "pause", "yes");
        } else if (playing) {  // application is on top
            command("set", "pause", "no");
        }
    });
}

void MPVCore::clean() {
    check_error(mpv_command_string(this->mpv, "quit"));
    brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);

    brls::Logger::info("trying free mpv context");
    if (this->mpv_context) {
        mpv_render_context_free(this->mpv_context);
        this->mpv_context = nullptr;
    }

    brls::Logger::info("trying terminate mpv");
    if (this->mpv) {
        mpv_terminate_destroy(this->mpv);
        // mpv_destroy(this->mpv);
        this->mpv = nullptr;
    }
}

void MPVCore::restart() {
    this->clean();
    this->init();
}

MPVMap MPVCore::supportCodecs() {
    MPVMap codecs;
    mpv_node node;
    mpv_get_property(mpv, "decoder-list", MPV_FORMAT_NODE, &node);
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        mpv_node_list *codec_list = node.u.list;
        for (int i = 0; i < codec_list->num; i++) {
            if (codec_list->values[i].format == MPV_FORMAT_NODE_MAP) {
                mpv_node_list *codec_map = codec_list->values[i].u.list;
                std::string name, desc;
                for (int n = 0; n < codec_map->num; n++) {
                    char *key = codec_map->keys[n];
                    if (strcmp(key, "codec") == 0) {
                        name = codec_map->values[n].u.string;
                    } else if (strcmp(key, "description") == 0) {
                        desc = codec_map->values[n].u.string;
                    }
                }
                codecs.insert(std::make_pair(name, desc));
            }
        }
    }
    mpv_free_node_contents(&node);
    return codecs;
}

void MPVCore::setFrameSize(brls::Rect rect) {
#ifdef MPV_SW_RENDER
#ifdef BOREALIS_USE_D3D11
    // 使用 dx11 的拷贝交换，否则视频渲染异常
    const static int mpvImageFlags = NVG_IMAGE_STREAMING | NVG_IMAGE_COPY_SWAP;
#else
    const static int mpvImageFlags = 0;
#endif
    int drawWidth = rect.getWidth() * brls::Application::windowScale;
    int drawHeight = rect.getHeight() * brls::Application::windowScale;
    if (drawWidth == 0 || drawHeight == 0) return;
    int frameSize = drawWidth * drawHeight;

    if (pixels != nullptr && frameSize > sw_size[0] * sw_size[1]) {
        brls::Logger::debug("Enlarge video surface buffer");
        free(pixels);
        pixels = nullptr;
    }

    if (pixels == nullptr) {
        pixels = malloc(frameSize * PIXCEL_SIZE);
        mpv_params[3].data = pixels;
    }

    auto vg = brls::Application::getNVGContext();
    if (nvg_image) nvgDeleteImage(vg, nvg_image);
    nvg_image = nvgCreateImageRGBA(vg, drawWidth, drawHeight, mpvImageFlags, (const unsigned char *)pixels);

    sw_size[0] = drawWidth;
    sw_size[1] = drawHeight;
    pitch = PIXCEL_SIZE * drawWidth;
#endif
}

bool MPVCore::isValid() { return mpv_context != nullptr; }

void MPVCore::draw(brls::Rect rect, float alpha) {
    if (mpv_context == nullptr) return;

#ifdef MPV_SW_RENDER
    if (!pixels) return;
    mpv_render_context_render(this->mpv_context, mpv_params);
    mpv_render_context_report_swap(this->mpv_context);

    auto *vg = brls::Application::getNVGContext();
    nvgUpdateImage(vg, nvg_image, (const unsigned char *)pixels);

    // draw black background
    nvgBeginPath(vg);
    nvgFillColor(vg, NVGcolor{0, 0, 0, alpha});
    nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
    nvgFill(vg);

    // draw video
    nvgBeginPath(vg);
    nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
    nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, rect.getWidth(), rect.getHeight(), 0, nvg_image, alpha));
    nvgFill(vg);
#else
    // 只在非透明时绘制视频，可以避免退出页面时视频画面残留
    if (alpha >= 1) {
#ifndef BOREALIS_USE_D3D11
        // Using default framebuffer
        this->mpv_fbo.w = brls::Application::windowWidth;
        this->mpv_fbo.h = brls::Application::windowHeight;
#endif
#ifdef BOREALIS_USE_DEKO3D
        static auto videoContext =
            dynamic_cast<brls::SwitchVideoContext *>(brls::Application::getPlatform()->getVideoContext());
        this->mpv_fbo.tex = videoContext->getFramebuffer();
        videoContext->queueSignalFence(&readyFence);
        videoContext->queueFlush();
#endif
        // 绘制视频
        mpv_render_context_render(this->mpv_context, mpv_params);
#ifdef BOREALIS_USE_D3D11
        D3D11_CONTEXT->beginFrame();
#elif defined(BOREALIS_USE_DEKO3D)
        videoContext->queueWaitFence(&doneFence);
#else
        glViewport(0, 0, brls::Application::windowWidth, brls::Application::windowHeight);
#endif
        mpv_render_context_report_swap(this->mpv_context);
    }
#endif

    if (BOTTOM_BAR) {
        NVGcontext *vg = brls::Application::getNVGContext();
        bottomBarColor.a = alpha;
        nvgFillColor(vg, bottomBarColor);
        nvgBeginPath(vg);
        nvgRect(vg, rect.getMinX(), rect.getMaxY() - 2, rect.getWidth() * (playback_time / duration), 2);
        nvgFill(vg);
    }
}

std::string MPVCore::getCacheSpeed() const {
    if (cache_speed >> 20 > 0) {
        return fmt::format("{:.2f} MB/s", (cache_speed >> 10) / 1024.0f);
    } else if (cache_speed >> 10 > 0) {
        return fmt::format("{:.2f} KB/s", cache_speed / 1024.0f);
    } else {
        return fmt::format("{} B/s", cache_speed);
    }
}

void MPVCore::eventMainLoop() {
    while (true) {
        mpv_event *event = mpv_wait_event(this->mpv, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_LOG_MESSAGE: {
            auto log = (mpv_event_log_message *)event->data;
            if (log->log_level <= MPV_LOG_LEVEL_ERROR) {
                brls::Logger::error("{}: {}", log->prefix, log->text);
            } else if (log->log_level <= MPV_LOG_LEVEL_WARN) {
                brls::Logger::warning("{}: {}", log->prefix, log->text);
            } else if (log->log_level <= MPV_LOG_LEVEL_INFO) {
                brls::Logger::info("{}: {}", log->prefix, log->text);
            } else if (log->log_level <= MPV_LOG_LEVEL_V) {
                brls::Logger::debug("{}: {}", log->prefix, log->text);
            } else {
                brls::Logger::verbose("{}: {}", log->prefix, log->text);
            }
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            brls::Logger::debug("MPVCore => EVENT_SHUTDOWN");
            return;
        case MPV_EVENT_FILE_LOADED:
            brls::Logger::info("MPVCore => EVENT_FILE_LOADED");
            // event 8: 文件预加载结束，准备解码
            mpvCoreEvent.fire(MpvEventEnum::MPV_LOADED);
            break;
        case MPV_EVENT_START_FILE:
            // event 6: 开始加载文件
            brls::Logger::info("MPVCore => EVENT_START_FILE");
            mpvCoreEvent.fire(MpvEventEnum::START_FILE);
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            // event 21: 开始播放文件（一般是播放或调整进度结束之后触发）
            brls::Logger::info("MPVCore => EVENT_PLAYBACK_RESTART");
            this->video_stopped = false;
            if (this->isPaused())
                mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
            else
                mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
            break;
        case MPV_EVENT_END_FILE: {
            // event 7: 文件播放结束
            this->video_stopped = true;
            auto node = (mpv_event_end_file *)event->data;
            if (node->reason == MPV_END_FILE_REASON_ERROR) {
                brls::Logger::error("MPVCore => FILE ERROR: {}", mpv_error_string(node->error));
                this->stop();
                mpvCoreEvent.fire(MpvEventEnum::MPV_FILE_ERROR);
            } else if (node->reason == MPV_END_FILE_REASON_EOF) {
                brls::Logger::info("MPVCore => END_OF_FILE");
                mpvCoreEvent.fire(MpvEventEnum::END_OF_FILE);
            } else {
                brls::Logger::info("MPVCore => STOP");
                mpvCoreEvent.fire(MpvEventEnum::MPV_STOP);
            }
            break;
        }
        case MPV_EVENT_COMMAND_REPLY: {
            mpv_event_command *cmd = (mpv_event_command *)event->data;
            if (event->error) {
                brls::Logger::error("MPVCore => COMMAND ERROR: {}", mpv_error_string(event->error));
                break;
            }
            if (event->reply_userdata > 0 && cmd->result.format == MPV_FORMAT_NODE_MAP) {
                mpv_node_list *node_list = cmd->result.u.list;
                for (int i = 0; i < node_list->num; i++) {
                    std::string key = node_list->keys[i];
                    auto &value = node_list->values[i];
                    if (key == "playlist_entry_id" && value.format == MPV_FORMAT_INT64) {
                        this->mpvCommandReply.fire(event->reply_userdata, value.u.int64);
                    }
                }
            }
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            /// https://mpv.io/manual/stable/#property-list
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (prop->format == MPV_FORMAT_NONE) break;

            switch (event->reply_userdata) {
            case 1:  // core-idle
                if (!*(int *)prop->data) {
                    mpvCoreEvent.fire(MpvEventEnum::LOADING_END);
                } else if (!this->video_stopped) {
                    brls::Logger::debug("MPVCore => IDLE");
                    mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
                }
                break;
            case 2:  // pause
                if (!!*(int *)prop->data) {
                    brls::Logger::info("MPVCore => PAUSE");
                    mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
                } else if (!this->video_stopped) {
                    brls::Logger::info("MPVCore => RESUME");
                    mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
                }
                break;
            case 3:  // duration
                duration = *(int64_t *)prop->data;
                if (duration >= 0) {
                    brls::Logger::debug("MPVCore => DURATION: {}", duration);
                    mpvCoreEvent.fire(MpvEventEnum::UPDATE_DURATION);
                }
                break;
            case 4:  // playback-time
                this->playback_time = *(double *)prop->data;
                if (video_progress != (int64_t)playback_time) {
                    video_progress = (int64_t)playback_time;
                    mpvCoreEvent.fire(MpvEventEnum::UPDATE_PROGRESS);
                }
                break;
            case 5:  // cache-speed
                this->cache_speed = *(int64_t *)prop->data;
                mpvCoreEvent.fire(MpvEventEnum::CACHE_SPEED_CHANGE);
                break;
            case 9:  // speed
                this->video_speed = *(double *)prop->data;
                mpvCoreEvent.fire(VIDEO_SPEED_CHANGE);
                break;
            case 10:  // volume
                if (*(int64_t *)prop->data > 0 && this->volume == 0) {
                    mpvCoreEvent.fire(VIDEO_UNMUTE);
                } else if (*(int64_t *)prop->data == 0 && this->volume > 0) {
                    mpvCoreEvent.fire(VIDEO_MUTE);
                }
                this->volume = *(int64_t *)prop->data;
            default:
                brls::Logger::debug("MPVCore => PROPERTY_CHANGE `{}` type {}", prop->name, int(prop->format));
            }
            break;
        }
        default:;
        }
    }
}

void MPVCore::reset() {
    brls::Logger::debug("MPVCore::reset");
    mpvCoreEvent.fire(MpvEventEnum::RESET);
    this->video_stopped = true;
    this->duration = 0;     // second
    this->cache_speed = 0;  // Bps
    this->playback_time = 0;
    this->video_progress = 0;
}

void MPVCore::setUrl(const std::string &url, const std::string &extra, const std::string &method, uint64_t userdata) {
    brls::Logger::debug("{} Url: {}, extra: {}", method, url, extra);
    if (mpv_client_api_version() >= MPV_MAKE_VERSION(2, 3)) {
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), "0", extra.c_str(), nullptr};
        mpv_command_async(this->mpv, userdata, cmd);
    } else {
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), extra.c_str(), nullptr};
        mpv_command_async(this->mpv, userdata, cmd);
    }
}

void MPVCore::togglePlay() { this->command("cycle", "pause"); }

void MPVCore::stop() { this->command("stop"); }

void MPVCore::seek(int64_t value, const std::string &flags) {
    std::string pos = std::to_string(value);
    this->command("seek", pos.c_str(), flags.c_str());
}

bool MPVCore::isStopped() const { return video_stopped; }

bool MPVCore::isPaused() {
    int ret = -1;
    mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &ret);
    return ret == 1;
}

double MPVCore::getSpeed() const { return video_speed; }

void MPVCore::setSpeed(double value) {
    std::string speed = std::to_string(value);
    this->command("set", "speed", speed.c_str());
}

void MPVCore::setAspect(const std::string &value) {
    if (value == "auto") {
        this->command("set", "keepaspect", "yes");
        this->command("set", "video-aspect-override", "no");
        this->command("set", "panscan", "0.0");
    } else if (value == "stretch") {  // 拉伸全屏
        this->command("set", "keepaspect", "no");
        this->command("set", "video-aspect-override", "-1");
        this->command("set", "panscan", "0.0");
    } else if (value == "crop") {  // 裁剪填充
        this->command("set", "keepaspect", "yes");
        this->command("set", "video-aspect-override", "-1");
        this->command("set", "panscan", "1.0");
    } else if (!value.empty()) {
        this->command("set", "keepaspect", "yes");
        this->command("set", "video-aspect-override", value.c_str());
        this->command("set", "panscan", "0.0");
    }
}

std::string MPVCore::getString(const std::string &key) {
    char *value = nullptr;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_STRING, &value);
    if (!value) return "";
    std::string result = std::string{value};
    mpv_free(value);
    return result;
}

double MPVCore::getDouble(const std::string &key) {
    double value = 0;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_DOUBLE, &value);
    return value;
}

void MPVCore::setDouble(const std::string &key, double value) {
    mpv_set_property_async(mpv, 0, key.c_str(), MPV_FORMAT_DOUBLE, &value);
}

int64_t MPVCore::getInt(const std::string &key, int64_t default_value) {
    int64_t value = default_value;
    mpv_get_property(mpv, key.c_str(), MPV_FORMAT_INT64, &value);
    return value;
}

void MPVCore::setInt(const std::string &key, int64_t value) {
    mpv_set_property_async(mpv, 0, key.c_str(), MPV_FORMAT_INT64, &value);
}

std::unordered_map<std::string, mpv_node> MPVCore::getNodeMap(const std::string &key) {
    mpv_node node;
    std::unordered_map<std::string, mpv_node> nodeMap;
    if (mpv_get_property(mpv, key.c_str(), MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_MAP) {
            mpv_node_list *node_list = node.u.list;
            for (int i = 0; i < node_list->num; i++) {
                std::string key = node_list->keys[i];
                mpv_node &value = node_list->values[i];
                nodeMap.insert(std::make_pair(key, value));
            }
        }
        mpv_free_node_contents(&node);
    }
    return nodeMap;
}

void MPVCore::setShader(const std::string &profile, const std::string &shaders, bool showHint) {
    brls::Logger::info("Set shader [{}]: {}", profile, shaders);
    if (shaders.empty()) return;
    this->command("no-osd", "change-list", "glsl-shaders", "set", shaders.c_str());
    if (showHint) showOsdText(profile);
}

void MPVCore::clearShader(bool showHint) {
    brls::Logger::info("Clear shader");
    this->command("no-osd", "change-list", "glsl-shaders", "clr", "");
    if (showHint) showOsdText("Clear shader");
}

void MPVCore::showOsdText(const std::string &value, int duration) {
    std::string d = std::to_string(duration);
    this->command("show-text", value.c_str(), d.c_str());
}