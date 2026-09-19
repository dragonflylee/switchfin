//
// Created by fang on 2022/8/12.
//

#include "view/mpv_core.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_startup.hpp"
#include "utils/ps5_native_heap_failure.hpp"
#endif
#include "api/http.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#ifdef PS5_NATIVE_GPU
#include <cstring>
#endif
#include <fmt/ranges.h>
#ifdef PS5_NATIVE_GPU
#include <limits>
#include <cstdlib>
#include <filesystem>
#include "utils/ps5_native_decoder_budget.hpp"
#include "utils/ps5_audio_policy.hpp"
#include "ps5_native_audio_config.hpp"
#include <SDL2/SDL_hints.h>
#endif

static inline void check_error(int status) {
    if (status < 0) brls::Logger::error("MPV ERROR => {}", mpv_error_string(status));
#ifdef PS5_NATIVE_GPU
}

static bool applyNativeAudioPolicy(mpv_handle* handle, bool initialized, const std::string& preference) {
    // Retained read-only token binds this compiled policy to dependency evidence.
    brls::Logger::info("ps5: audio build {}", PS5_NATIVE_AUDIO_BUILD_TOKEN);
    return ps5_audio_policy::configure(PS5_NATIVE_AUDIO_51_AVAILABLE != 0, preference,
        [](const char* value) {
            return SDL_SetHintWithPriority("SWITCHFIN_PS5_AUDIO_51", value, SDL_HINT_OVERRIDE) == SDL_TRUE;
        },
        [handle, initialized](const char* channels) {
            return (initialized ? mpv_set_property_string(handle, "audio-channels", channels)
                                : mpv_set_option_string(handle, "audio-channels", channels)) >= 0;
        });
}

// Consume borrowed event storage before mpv_wait_event invalidates it. Subtitle
// text is deliberately reduced to a byte count; no cue text enters the cache.
static void cacheProfileProperty(ObservedProperties& properties, uint64_t epoch,
    const mpv_event_property& property) {
    if (!properties.accepts(epoch) || !property.name) return;
    if (std::strcmp(property.name, "demuxer-cache-state") == 0) {
        // mpv 0.36 exposes this as a node map; its property handler does not
        // implement slash-key queries. Copy only the two numeric values while
        // the event owns the map. Missing/malformed fields retire old readings.
        const auto* node = property.format == MPV_FORMAT_NODE && property.data
            ? static_cast<const mpv_node*>(property.data) : nullptr;
        const auto* map = node && node->format == MPV_FORMAT_NODE_MAP ? node->u.list : nullptr;
        for (const char* key : {"total-bytes", "fw-bytes"}) {
            const std::string name = std::string("demuxer-cache-state/") + key;
            const mpv_node* value = nullptr;
            bool valid = map && map->num >= 0 && map->num <= 64 &&
                (!map->num || (map->keys && map->values));
            if (valid) {
                for (int i = 0; i < map->num; ++i) {
                    if (!map->keys[i]) { valid = false; break; }
                    if (std::strcmp(map->keys[i], key) != 0) continue;
                    if (value) { valid = false; break; }
                    value = &map->values[i];
                }
            }
            if (valid && value && value->format == MPV_FORMAT_INT64 && value->u.int64 >= 0)
                properties.update(epoch, name, value->u.int64);
            else
                properties.unavailable(epoch, name);
        }
    } else if (std::strcmp(property.name, "sub-text") == 0) {
        if (property.format == MPV_FORMAT_STRING && property.data &&
            *static_cast<char**>(property.data)) {
            properties.update(epoch, "sub-text-bytes",
                static_cast<int64_t>(std::strlen(*static_cast<char**>(property.data))));
        } else {
            properties.unavailable(epoch, "sub-text-bytes");
        }
    } else if (!property.data) {
        properties.unavailable(epoch, property.name);
    } else if (property.format == MPV_FORMAT_FLAG) {
        properties.update(epoch, property.name, int64_t{*static_cast<int*>(property.data) != 0});
    } else if (property.format == MPV_FORMAT_INT64) {
        properties.update(epoch, property.name, *static_cast<int64_t*>(property.data));
    } else if (property.format == MPV_FORMAT_DOUBLE) {
        properties.update(epoch, property.name, *static_cast<double*>(property.data));
    } else if (property.format == MPV_FORMAT_STRING && *static_cast<char**>(property.data)) {
        properties.update(epoch, property.name, std::string(*static_cast<char**>(property.data)));
    } else {
        properties.unavailable(epoch, property.name);
    }
#endif
}

#ifndef MPV_SW_RENDER
#ifdef BOREALIS_USE_D3D11
#include <borealis/platforms/driver/d3d11.hpp>
extern std::unique_ptr<brls::D3D11Context> D3D11_CONTEXT;
#elif defined(BOREALIS_USE_DEKO3D)
#include <borealis/platforms/switch/switch_video.hpp>
#elif defined(BOREALIS_USE_GXM)
#include <borealis/platforms/psv/psv_video.hpp>
#include <borealis/extern/nanovg/nanovg_gxm.h>
#else
#ifdef __SDL2__
#include <SDL2/SDL.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(__PS4__) || defined(__PSV__) || defined(__SWITCH__) || defined(ANDROID)
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#endif
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

#ifdef ANDROID
#include <jni.h>
extern "C" {
#include <libavcodec/jni.h>
}
static JavaVM *g_vm;
static jobject surface;

static int64_t getNativeSurface() {
    int64_t ptr = 0;
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    jclass cls = env->GetObjectClass(activity);
    jmethodID jmethod = env->GetStaticMethodID(cls, "getMpvSurface", "()Landroid/view/Surface;");
    jobject surface_ = env->CallStaticObjectMethod(cls, jmethod);
    if (surface_ != nullptr) {
        surface = env->NewGlobalRef(surface_);
        ptr = (int64_t)(intptr_t)surface;
        env->DeleteLocalRef(surface_);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ptr;
}

static void deleteSurfaceObj() {
    auto env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    env->DeleteGlobalRef(surface);
}
#endif

#ifdef PS5_NATIVE_GPU
void MPVCore::on_update(void *self) noexcept {
    auto* context = static_cast<CallbackContext*>(self);
    context->notifications.notify(Ps5NativeMpvNotifications::Render);
#else
void MPVCore::on_update(void *self) {
    MPVCore *mpv = reinterpret_cast<MPVCore *>(self);
    brls::sync([mpv]() {
        uint64_t flags = mpv_render_context_update(mpv->mpv_context);
#if defined(MPV_SW_RENDER) || defined(BOREALIS_USE_GXM)
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            mpv_render_context_render(mpv->mpv_context, mpv->mpv_params);
            mpv_render_context_report_swap(mpv->mpv_context);
        }
#else
        (void)flags;
#endif
    });
#endif
}

#ifdef PS5_NATIVE_GPU
void MPVCore::on_wakeup(void *self) noexcept {
    auto* context = static_cast<CallbackContext*>(self);
    context->notifications.notify(Ps5NativeMpvNotifications::Events);
}

void MPVCore::drainNativeCallbacks() noexcept {
    static bool draining = false; // UI-only; nested UI loops must not invalidate borrowed MPV events.
    if (draining) return;
    MPVCore* owner = nativeCallbackOwner;
    if (!owner || !owner->callbacks) return;
    struct DrainGuard { bool& active; ~DrainGuard() { active = false; } } guard{draining};
    draining = true;
    // Keep the session identity alive, not a raw userdata pointer: an event
    // listener may clean/restart the player and destroy its callback context.
    auto session = owner->callbacks->gate;
    const unsigned pending = owner->callbacks->notifications.take();
    auto current = [&]() noexcept {
        return nativeCallbackOwner == owner && owner->callbacks &&
            owner->callbacks->gate == session && session->isActive();
    };
    auto retry = [&](unsigned channel) noexcept {
        if (current()) owner->callbacks->notifications.notify(channel);
        static unsigned reports = 0; // UI-only; never format exception contents.
        if (reports < 16) {
            ++reports;
            ps5_native_startup::detail::line("MPV-CALLBACK error-stage=%u report=%u\n", channel, reports);
        }
    };
    if ((pending & Ps5NativeMpvNotifications::Events) && current()) {
        try { owner->eventMainLoop(); }
        catch (...) { retry(Ps5NativeMpvNotifications::Events); }
    }
    if ((pending & Ps5NativeMpvNotifications::Render) && current() && owner->mpv_context) {
        try {
            const uint64_t flags = mpv_render_context_update(owner->mpv_context);
#if defined(MPV_SW_RENDER) || defined(BOREALIS_USE_GXM)
            if (flags & MPV_RENDER_UPDATE_FRAME) {
                {
                    mpv_render_context_render(owner->mpv_context, owner->mpv_params);
                }
#ifdef MPV_SW_RENDER
                owner->sw_dirty = true;
#endif
                owner->presentationPending = true;
            }
#else
            (void)flags;
#endif
        } catch (...) { retry(Ps5NativeMpvNotifications::Render); }
    }
#else
void MPVCore::on_wakeup(void *self) {
    MPVCore *mpv = reinterpret_cast<MPVCore *>(self);
    brls::sync([mpv]() { mpv->eventMainLoop(); });
#endif
}

MPVCore::MPVCore() {
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
#ifdef PS5_NATIVE_GPU
    if (this->mpv) return;
    playbackState = {};
    ps5_native_heap_checkpoint(Ps5HeapPhase::InitBegin);
    this->callbacks = std::make_unique<CallbackContext>();
    this->callbacks->owner = this;
    this->profileProperties.reset();
#endif
    std::setlocale(LC_NUMERIC, "C");
#ifdef ANDROID
    auto env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env->GetJavaVM(&g_vm) && g_vm) av_jni_set_java_vm(g_vm, NULL);
#endif
    this->mpv = mpv_create();
    if (!mpv) {
        brls::fatal("Error Create mpv Handle");
    }

#ifdef PS5_NATIVE_GPU
    ps5_native_heap_checkpoint(Ps5HeapPhase::HandleCreated);
#endif
    auto &conf = AppConfig::instance();
    std::string confDir = conf.configDir();

    // misc
    mpv_set_option_string(mpv, "config", "yes");
    mpv_set_option_string(mpv, "config-dir", confDir.c_str());
#ifdef PS5_NATIVE_GPU
    // Subtitles rendered as nothing here, and pointing sub-fonts-dir at the
    // bundle's fonts was not enough on its own.
    //
    // mpv hands libass two things: a fonts directory to scan, and a *default
    // font file* which it looks up as "<config-dir>/subfont.ttf" (see
    // mp_ass_configure_fonts in sub/ass_mp.c). With sub-font-provider=none --
    // which this target needs, because fontconfig does not exist -- that default
    // file is what libass falls back to when family matching finds nothing, and
    // it is the only thing guaranteed to render. Nothing ships one, so seed it
    // from a font the bundle already carries.
    {
        // Raw IO, because std::filesystem::copy_file reported success through an
        // error_code that was swallowed while embedded ASS subtitles still
        // rendered as tofu -- the fallback file libass falls back to was never
        // actually written. opendir/stat/fopen bind on this image; the
        // std::filesystem wrapper did not do what it claimed.
        const std::string subfont = confDir + "/subfont.ttf";
        struct stat st {};
        if (::stat(subfont.c_str(), &st) != 0 || st.st_size == 0) {
            ::mkdir(confDir.c_str(), 0700);
            const std::string source = BRLS_ASSET("font/switch_font.ttf");
            FILE* in = std::fopen(source.c_str(), "rb");
            FILE* out = in ? std::fopen(subfont.c_str(), "wb") : nullptr;
            bool ok = false;
            if (in && out) {
                char buffer[64 * 1024];
                size_t n;
                ok = true;
                while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0)
                    if (std::fwrite(buffer, 1, n, out) != n) { ok = false; break; }
                if (ok && std::ferror(in)) ok = false;
            }
            if (out) { if (std::fflush(out) != 0) ok = false; std::fclose(out); }
            if (in) std::fclose(in);
            brls::Logger::info("mpv: seeding libass default font at {}: {}", subfont, ok ? "ok" : "failed");
        }
    }
    // Still useful: this is what family names resolve against, so a subtitle
    // asking for a specific face can find one instead of using the default.
    // Native assets are absolute /app0 paths; the payload anchors its cwd.
    mpv_set_option_string(mpv, "sub-fonts-dir", BRLS_ASSET("font").c_str());
#else
    mpv_set_option_string(mpv, "sub-fonts-dir", confDir.c_str());
#endif
    mpv_set_option_string(mpv, "watch-later-dir", fmt::format("{}/watch-later", confDir).c_str());
    mpv_set_option_string(mpv, "gpu-shader-cache-dir", fmt::format("{}/cache", confDir).c_str());
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "referrer", conf.getUrl().c_str());
    mpv_set_option_string(mpv, "user-agent", HTTP::USER_AGENT.c_str());
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "video-timing-offset", "0");  // 60fps
    mpv_set_option_string(mpv, "reset-on-next-file", "speed,pause");
    mpv_set_option_string(mpv, "subs-fallback", SUBS_FALLBACK ? "yes" : "no");
    mpv_set_option_string(mpv, "vo", MPVCore::VO.c_str());
#ifdef PS5_NATIVE_GPU
    // Native audio policy before initialization; only explicit 5.1 can opt in.
    if (!applyNativeAudioPolicy(mpv, false, MPVCore::AUDIO_CHANNELS)) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        brls::fatal("Could not configure PS5 audio");
    }
    // This target provides SDL audio. Avoid probing the absent /dev/dsp OSS
    // device before falling back to the same SDL backend on every file.
    mpv_set_option_string(mpv, "ao", "sdl");
#else
#if defined(__PS4__) || defined(__PSV__) || defined(TRIMUI)
    mpv_set_option_string(mpv, "audio-channels", "stereo");
#else
    mpv_set_option_string(mpv, "audio-channels", MPVCore::AUDIO_CHANNELS.c_str());
#endif
#endif

#ifdef PS5_NATIVE_GPU
    bool lowQuality = MPVCore::LOW_QUALITY;
    // Allow the launcher to override the saved decoder-quality preference.
    if (const char* env = std::getenv("MPV_LOW_QUALITY")) lowQuality = std::atoi(env) != 0;

    if (lowQuality) {
#else
    if (MPVCore::LOW_QUALITY) {
#endif
        // Less cpu cost
        brls::Logger::info("lavc: skip loop filter and set fast decode");
        mpv_set_option_string(mpv, "vd-lavc-skiploopfilter", "all");
        mpv_set_option_string(mpv, "vd-lavc-fast", "yes");
        if (mpv_client_api_version() > MPV_MAKE_VERSION(2, 1)) {
            mpv_set_option_string(mpv, "profile", "fast");
        }
#ifdef PS5_NATIVE_GPU
    } else {
        brls::Logger::info("lavc: full quality, in-loop deblocking on");
#endif
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
#ifdef PS5_NATIVE_GPU
#if defined(__SWITCH__)
    mpv_set_option_string(mpv, "vd-lavc-dr", "yes");
    mpv_set_option_string(mpv, "vd-lavc-threads", "3");
    // This should fix random crash, but I don't know why.
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");
    // Set default subfont
    std::string locale = brls::Application::getPlatform()->getLocale();
    if (locale == brls::LOCALE_ZH_HANS)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_org_zh-cn_003");
    else if (locale == brls::LOCALE_ZH_HANT)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udjxh-db_zh-tw_003");
    else if (locale == brls::LOCALE_Ko)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_ko_003");
#elif defined(__PS4__)
    mpv_set_option_string(mpv, "vd-lavc-threads", "6");
#else
    // 8c/16t Zen 2, but SDL spends 12 threads per frame swizzling the
    // framebuffer into the tiled scanout layout, so leave it headroom.
    // MPV_VD_THREADS overrides it, so the sweep that decides whether software
    // decode is the limit can be run without a rebuild -- if frame rate moves
    // with thread count, decode is the bottleneck; if it does not, it is not.
    {
        const char *threads = std::getenv("MPV_VD_THREADS");
        mpv_set_option_string(mpv, "vd-lavc-threads", threads ? threads : "6");
    }
    // Let libavcodec decode straight into buffers swscale can consume, saving a
    // full-frame copy per frame on a target that has none to spare.
    // Core3.3 uploads begin with ordinary CPU planes. Direct decoder buffers
    // require BufferStorage/fence semantics beyond the native graphics contract.
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "opengl-pbo", "no");
    // libass probes fontconfig, which does not exist here; without this every
    // playback start prints "Fontconfig error: Cannot load default config file".
    // sub-fonts-dir above is what actually supplies the fonts.
    mpv_set_option_string(mpv, "sub-font-provider", "none");
    // With no font provider, libass matches by family name against whatever is
    // in sub-fonts-dir, and mpv's default family ("sans-serif") matches nothing
    // there. resources/font/switch_font.ttf is DejaVu Sans despite the name;
    // the CJK fallbacks in the same directory are picked up automatically.
    mpv_set_option_string(mpv, "sub-font", "DejaVu Sans");
#endif
#else
#if defined(__SWITCH__)
    mpv_set_option_string(mpv, "vd-lavc-dr", "yes");
    mpv_set_option_string(mpv, "vd-lavc-threads", "3");
    // This should fix random crash, but I don't know why.
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");
    // Set default subfont
    std::string locale = brls::Application::getPlatform()->getLocale();
    if (locale == brls::LOCALE_ZH_HANS)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_org_zh-cn_003");
    else if (locale == brls::LOCALE_ZH_HANT)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udjxh-db_zh-tw_003");
    else if (locale == brls::LOCALE_Ko)
        mpv_set_option_string(mpv, "sub-font", "nintendo_udsg-r_ko_003");
#elif defined(__PS4__)
    mpv_set_option_string(mpv, "vd-lavc-threads", "6");
#elif defined(__PSV__)
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(mpv, "fbo-format", "rgba8");
    // Fix vo_wait_frame() cannot be wakeup
    mpv_set_option_string(mpv, "video-latency-hacks", "yes");
#elif defined(ANDROID)
    mpv_set_option_string(mpv, "gpu-context", "android");
    mpv_set_option_string(mpv, "opengl-es", "yes");
#endif
#endif

    // hardware decoding
    if (MPVCore::HARDWARE_DEC) {
        mpv_set_option_string(mpv, "hwdec", PLAYER_HWDEC_METHOD.c_str());
        brls::Logger::info("MPV hardware decode: {}", PLAYER_HWDEC_METHOD);
    } else {
        mpv_set_option_string(mpv, "hwdec", "no");
    }

    if (MPVCore::DEBUG) {
        mpv_set_option_string(mpv, "terminal", "yes");
#ifdef PS5_NATIVE_GPU
        // all=v is a lot of output, and on targets where stdout is a pipe that
        // someone has to keep reading (PS5 under websrv) the volume is itself a
        // hazard. MPV_MSG_LEVEL narrows it without a rebuild -- e.g.
        // "all=no,ass=v,sd_ass=v" to look at nothing but subtitle rendering.
        const char* level = std::getenv("MPV_MSG_LEVEL");
        mpv_set_option_string(mpv, "msg-level", level ? level : "all=v");
#else
        //  mpv_set_option_string(mpv, "msg-level", "all=no");
        mpv_set_option_string(mpv, "msg-level", "all=v");
#endif
    } else if (brls::Application::isDebuggingViewEnabled()) {
        mpv_request_log_messages(mpv, "info");
    }
#ifdef PS5_NATIVE_GPU
    // Let diagnostic launches retain decoder/renderer messages through the
    // application's file logger, without relying on the launcher's stdout pipe.
    if (const char* level = std::getenv("MPV_CLIENT_LOG_LEVEL"))
        mpv_request_log_messages(mpv, level);
#endif

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID)
    if (conf.getItem(AppConfig::SINGLE, false)) {
        mpv_set_option_string(mpv, "input-ipc-server", conf.ipcSocket().c_str());
    }
#endif

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
#ifdef PS5_NATIVE_GPU
        mpv = nullptr;
#endif
        brls::fatal("Could not initialize mpv context");
    }

#ifdef PS5_NATIVE_GPU
    ps5_native_heap_checkpoint(Ps5HeapPhase::Initialized);

    // mpv_initialize loads mpv.conf after the options above. Reapply the
    // platform requirements using the runtime property API, before creating
    // the render context or loading a file. Do not edit the user's config.
    // Native audio policy after configuration; reset both hint and mpv layout.
    if (!applyNativeAudioPolicy(mpv, true, MPVCore::AUDIO_CHANNELS)) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        brls::fatal("Could not configure PS5 audio");
    }
    const std::pair<const char*, const char*> requiredOptions[] = {
        {"vo", "libmpv"}, {"ao", "sdl"}, {"hwdec", "no"},
        {"vd-lavc-dr", "no"}, {"opengl-pbo", "no"},
        // mpv 0.36 normalizes linear output by target peak, including OSD.
        // Keep video and libass subtitles in the UI's shared 10000-nit scale.
        // PQ output renders directly into the ten-bit window when no UI
        // is drawn; otherwise into a PQ layer composited under the linear UI.
        {"target-prim", "bt.2020"}, {"target-trc", "pq"}, {"target-peak", "10000"},
        {"fbo-format", "rgba16f"}, {"dither-depth", "no"}, {"hdr-compute-peak", "no"},
        {"icc-profile", ""}, {"icc-profile-auto", "no"}, {"glsl-shaders", ""},
        {"gpu-dumb-mode", "no"}, {"blend-subtitles", "no"},
        {"background", "#000000"}, {"video-output-levels", "full"},
    };
    for (const auto& option : requiredOptions) {
        int status = mpv_set_property_string(mpv, option.first, option.second);
        if (status < 0) {
            brls::Logger::error("ps5: required mpv option {}={} failed: {}",
                option.first, option.second, mpv_error_string(status));
            mpv_terminate_destroy(mpv);
            mpv = nullptr;
            brls::fatal("Could not configure PS5 playback");
        }
    }
    brls::Logger::info("ps5: playback policy vo={} ao={} audio-channels={} hwdec={}",
        getString("options/vo"), getString("options/ao"),
        getString("options/audio-channels"), getString("options/hwdec"));

    // Configure and read back the native decoder budget after mpv.conf loads.
    // Keep an explicit one-thread preference; automatic/higher counts use two.
    if (!ps5_native_decoder_budget::configure(
            [this](const char* name, int64_t& value) {
                return mpv_get_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0;
            },
            [this](const char* name, int64_t value) {
                return mpv_set_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0;
            }, nativeDecoderThreads)) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        brls::fatal("Could not configure the native decoder memory budget");
    }
    ps5_native_startup::detail::line("DECODER-BUDGET schema=1 cap=2 effective=%lld\n",
        static_cast<long long>(nativeDecoderThreads));
    // Bound the application-owned heap without rewriting saved mpv.conf.
    // Disable backward-cache borrowing and enforce this same budget per file.
    if (!ps5_native_cache_budget::configure(
            [this](const char* name, int64_t& value, bool flag) {
                if (!flag) return mpv_get_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0;
                int observed = 0;
                const int status = mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &observed);
                value = observed;
                return status >= 0;
            },
            [this](const char* name, int64_t value, bool flag) {
                if (!flag) return mpv_set_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0;
                int selected = value != 0;
                return mpv_set_property(mpv, name, MPV_FORMAT_FLAG, &selected) >= 0;
            }, nativeCacheLimits)) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        brls::fatal("Could not configure the native demux memory budget");
    }
    ps5_native_startup::detail::line("CACHE-BUDGET schema=1 forward=%lld backward=%lld donate=0\n",
        static_cast<long long>(nativeCacheLimits.forward), static_cast<long long>(nativeCacheLimits.backward));
    brls::Logger::info("ps5 native: vd-lavc-dr={} opengl-pbo={} cache-forward={} cache-back={} bytes",
        getString("options/vd-lavc-dr"), getString("options/opengl-pbo"),
        getInt("demuxer-max-bytes"), getInt("demuxer-max-back-bytes"));

    // Only saved appearance choices override mpv.conf. Capture its values once
    // per mpv instance so choosing Default restores them without editing it.
    subtitlePreferences = {getDouble("sub-font-size"), getInt("sub-margin-y", 22)};
    this->applySubtitlePreferences();

    if (std::getenv("MPV_CLIENT_LOG_LEVEL")) {
        // Observe the backends actually selected when media opens, separately
        // from the configured policy. No synchronous queries during rendering.
        for (const char* property : {"current-vo", "current-ao", "hwdec-current"})
            check_error(mpv_observe_property(mpv, 110, property, MPV_FORMAT_STRING));
    }

    ps5_native_heap_checkpoint(Ps5HeapPhase::Configured);
#endif
    this->setAspect(VIDEO_ASPECT);

    // set observe properties
    check_error(mpv_observe_property(mpv, 1, "core-idle", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 2, "pause", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 3, "duration", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 4, "playback-time", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 5, "cache-speed", MPV_FORMAT_INT64));
#ifdef PS5_NATIVE_GPU
    check_error(mpv_observe_property(mpv, 6, "idle-active", MPV_FORMAT_FLAG));
#endif
    check_error(mpv_observe_property(mpv, 9, "speed", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 10, "volume", MPV_FORMAT_INT64));
#ifdef PS5_NATIVE_GPU
    this->observeProfileProperties();
#endif

// init renderer params
#ifdef ANDROID
    int64_t wid = getNativeSurface();
    mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, (void *)&wid);
    mpv_set_option_string(mpv, "force-window", "yes");
#elif defined(MPV_SW_RENDER)
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#elif defined(BOREALIS_USE_D3D11)
    mpv_d3d11_init_params init_params{.device = D3D11_CONTEXT->getDevice()};
    const char *backend = conf.getItem(AppConfig::MPV_RENDER, false) ? "gpu-next" : "gpu";
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_D3D11)},
        {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &init_params},
        {MPV_RENDER_PARAM_BACKEND, const_cast<char *>(backend)},
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
#elif defined(BOREALIS_USE_GXM)
    auto videoContext = dynamic_cast<brls::PsvVideoContext *>(brls::Application::getPlatform()->getVideoContext());
    NVGXMwindow *gxm = videoContext->getWindow();
    NVGcontext *vg = brls::Application::getNVGContext();
    mpv_gxm_init_params gxm_params = {
        .context = gxm->context,
        .shader_patcher = gxm->shader_patcher,
        .buffer_index = 0,
        .msaa = SCE_GXM_MULTISAMPLE_4X,
    };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_GXM)},
        {MPV_RENDER_PARAM_GXM_INIT_PARAMS, &gxm_params},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (!mpv_fbo.render_target) {
        int texture_width = DISPLAY_WIDTH;
        int texture_height = DISPLAY_HEIGHT;
        int texture_stride = ALIGN(texture_width, 8);
        nvg_image = nvgCreateImageRGBA(vg, texture_width, texture_height, 0, nullptr);
        NVGXMtexture *texture = nvgxmImageHandle(vg, nvg_image);

        NVGXMframebufferInitOptions framebufferOpts = {
            .display_buffer_count = 1,  // Must be 1 for custom FBOs
            .scenesPerFrame = 1,
            .render_target = texture,
            .color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
            .color_surface_type = SCE_GXM_COLOR_SURFACE_LINEAR,
            .display_width = texture_width,
            .display_height = texture_height,
            .display_stride = texture_stride,
        };
        NVGXMframebuffer *fbo = gxmCreateFramebuffer(&framebufferOpts);
        mpv_fbo.render_target = fbo->gxm_render_target;
        mpv_fbo.color_surface = &fbo->gxm_color_surfaces[0].surface;
        mpv_fbo.depth_stencil_surface = &fbo->gxm_depth_stencil_surface;
        mpv_fbo.w = texture_width;
        mpv_fbo.h = texture_height;
    }
#else
    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
#if defined(GLFW_EXPOSE_NATIVE_X11)
        {MPV_RENDER_PARAM_X11_DISPLAY, glfwGetX11Display()},
#endif
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        {MPV_RENDER_PARAM_WL_DISPLAY, glfwGetWaylandDisplay()},
#endif
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
#endif

#ifndef ANDROID
#ifdef PS5_NATIVE_GPU
    brls::Logger::info("mpv: creating render context ({})", params[0].data ?
        static_cast<const char*>(params[0].data) : "unknown");
#endif
    if (mpv_render_context_create(&mpv_context, mpv, params) < 0) {
        mpv_terminate_destroy(mpv);
#ifdef PS5_NATIVE_GPU
        mpv = nullptr;
#endif
        brls::fatal("failed to initialize mpv context");
    }
#ifdef PS5_NATIVE_GPU
    ps5_native_heap_checkpoint(Ps5HeapPhase::RenderContextCreated);
#endif
#endif
#ifdef BOREALIS_USE_D3D11
    misc::initCrashDump();
#endif
#ifdef PS5_NATIVE_GPU
    brls::Logger::info("version: {} ffmpeg {}", getString("mpv-version"), getString("ffmpeg-version"));
#else
    brls::Logger::info("version: {} ffmpeg {}", mpv_get_property_string(mpv, "mpv-version"),
        mpv_get_property_string(mpv, "ffmpeg-version"));
#endif

    this->command("set", "audio-client-name", AppVersion::getPackageName().c_str());
    // set event callback
#ifdef PS5_NATIVE_GPU
    mpv_set_wakeup_callback(mpv, on_wakeup, callbacks.get());
#else
    mpv_set_wakeup_callback(mpv, on_wakeup, this);
#endif
#ifndef ANDROID
    // set render callback
#ifdef PS5_NATIVE_GPU
    mpv_render_context_set_update_callback(mpv_context, on_update, callbacks.get());
#else
    mpv_render_context_set_update_callback(mpv_context, on_update, this);
#endif
#endif

    focusSubscription = brls::Application::getWindowFocusChangedEvent()->subscribe([this](bool focus) {
        static bool playing = false;
        if (!focus) {
            // application is sleep, save the current state
            playing = !isPaused();
            command("set", "pause", "yes");
        } else if (playing) {  // application is on top
            command("set", "pause", "no");
        }
#if defined(ANDROID)
        this->enableVO(focus);
#endif
    });

    sizeSubscription = brls::Application::getWindowSizeChangedEvent()->subscribe([this]() {
        // Docking/undocking the Switch swaps the framebuffer between 1280x720
        // and 1920x1080 while the borealis layout stays in 1280x720 points, so
        // the rect guard in draw() never fires; refresh mpv_fbo.w/h (pixels)
        // from the new Application::windowWidth/Height here.
        setFrameSize(this->rect);
    });
#ifdef PS5_NATIVE_GPU
    subscriptionsActive = true;
    brls::Application::getPlatform()->getVideoContext()->setPresentedCallback([this]() {
        if (presentationPending && mpv_context) {
            presentationPending = false;
            mpv_render_context_report_swap(mpv_context);
        }
    });
#endif

#if defined(BOREALIS_USE_OPENGL) && !defined(MPV_SW_RENDER)
    // Get default framebuffer
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &default_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
    mpv_fbo.fbo = default_framebuffer;
#endif
#ifdef PS5_NATIVE_GPU
    nativeCallbackOwner = this;
#endif
}

void MPVCore::clean() {
#ifdef PS5_NATIVE_GPU
    playbackState = {};
    if (nativeCallbackOwner == this) nativeCallbackOwner = nullptr;
    if (callbacks) callbacks->notifications.close();
    this->clearProfileProperties();
    if (!this->mpv && !this->mpv_context) return;
    if (callbacks) callbacks->gate->close();
    if (mpv) mpv_set_wakeup_callback(mpv, nullptr, nullptr);
    if (mpv_context) mpv_render_context_set_update_callback(mpv_context, nullptr, nullptr);
    presentationPending = false;
    brls::Application::getPlatform()->getVideoContext()->setPresentedCallback({});
    if (subscriptionsActive) {
        brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);
        brls::Application::getWindowSizeChangedEvent()->unsubscribe(sizeSubscription);
        subscriptionsActive = false;
    }
#else
    check_error(mpv_command_string(this->mpv, "quit"));
    brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);
    brls::Application::getWindowSizeChangedEvent()->unsubscribe(sizeSubscription);
#endif

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
#ifdef PS5_NATIVE_GPU
    // Keep the callback userdata alive until mpv has joined its workers.
    callbacks.reset();
#endif
#ifdef ANDROID
    deleteSurfaceObj();
#endif
}

void MPVCore::restart() {
    this->clean();
    this->init();

    setFrameSize(rect);
}

MPVMap MPVCore::supportCodecs() {
    MPVMap codecs;
#ifdef PS5_NATIVE_GPU
    mpv_node node{};
    if (!mpv || mpv_get_property(mpv, "decoder-list", MPV_FORMAT_NODE, &node) < 0) return codecs;
#else
    mpv_node node;
    mpv_get_property(mpv, "decoder-list", MPV_FORMAT_NODE, &node);
#endif
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

void MPVCore::setFrameSize(brls::Rect area) {
    rect = area;
    if (std::isnan(rect.getWidth()) || std::isnan(rect.getHeight())) return;

#ifdef MPV_SW_RENDER
#ifdef BOREALIS_USE_D3D11
    // 使用 dx11 的拷贝交换，否则视频渲染异常
    const static int mpvImageFlags = NVG_IMAGE_STREAMING | NVG_IMAGE_COPY_SWAP;
#else
#ifdef PS5_NATIVE_GPU
    // mpv renders at exactly sw_size and the quad below is drawn at exactly that
    // size, so the sampler is always 1:1 and bilinear filtering produces
    // bit-identical output -- while costing llvmpipe four texel fetches and a
    // lerp for every one of 2 million pixels.
    const static int mpvImageFlags = NVG_IMAGE_NEAREST;
#else
    const static int mpvImageFlags = 0;
#endif
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
#ifdef PS5_NATIVE_GPU
    // The buffer was just (re)allocated, so whatever the new texture was seeded
    // with is not a real frame; force one upload once mpv has filled it.
    sw_dirty = true;
#endif
#elif defined(BOREALIS_USE_GXM)
    // This line will be called between beginFrame() and endFrame() in Application::frame(),
    // but mpvRenderContextRender(...) will call functions similar to beginFrame() and endFrame() to draw content to FBO,
    // and that will cause error in GXM, so call in brls::sync to make the mpv drawing calls outside the brls::Application::frame().
#ifdef PS5_NATIVE_GPU
    if (callbacks) callbacks->gate->post(CallbackGate::Channel::Render, brls::sync, [this]() {
        if (!mpv_context) return;
#else
    brls::sync([this]() {
#endif
        mpv_render_context_render(this->mpv_context, mpv_params);
        mpv_render_context_report_swap(this->mpv_context);
    });
#elif !defined(BOREALIS_USE_D3D11)
    // Using default framebuffer
    this->mpv_fbo.w = brls::Application::windowWidth;
    this->mpv_fbo.h = brls::Application::windowHeight;
#endif
}

bool MPVCore::isValid() {
#ifdef ANDROID
    return true;
#else
    return mpv_context != nullptr;
#endif
}

void MPVCore::draw(brls::Rect area, float alpha) {
    if (mpv_context == nullptr) return;
    if (!(this->rect == area)) this->setFrameSize(area);

#ifdef MPV_SW_RENDER
    if (!pixels) return;

    auto *vg = brls::Application::getNVGContext();
#ifdef PS5_NATIVE_GPU
    if (sw_dirty.exchange(false)) {
        nvgUpdateImage(vg, nvg_image, (const unsigned char *)pixels);
    }
#else
    nvgUpdateImage(vg, nvg_image, (const unsigned char *)pixels);
#endif

#ifdef PS5_NATIVE_GPU
    // draw black background -- only where the video will not cover it anyway.
    // At alpha 1 this fill is entirely overdrawn by the video quad below, and on
    // a CPU rasteriser a wasted full-screen fill is not free.
    if (alpha < 1.0f) {
        nvgBeginPath(vg);
        nvgFillColor(vg, NVGcolor{0, 0, 0, alpha});
        nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
        nvgFill(vg);
    }
#else
    // draw black background
    nvgBeginPath(vg);
    nvgFillColor(vg, NVGcolor{0, 0, 0, alpha});
    nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
    nvgFill(vg);
#endif

    // draw video
    nvgBeginPath(vg);
    nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
    nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, rect.getWidth(), rect.getHeight(), 0, nvg_image, alpha));
    nvgFill(vg);
#elif defined(BOREALIS_USE_GXM)
    NVGcontext *vg = brls::Application::getNVGContext();
    NVGpaint img =
        nvgImagePattern(vg, area.getMinX(), area.getMinY(), area.getWidth(), area.getHeight(), 0, nvg_image, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, area.getMinX(), area.getMinY(), area.getWidth(), area.getHeight());
    nvgFillPaint(vg, img);
    nvgFill(vg);
#elif defined(ANDROID)
#else
    // 只在非透明时绘制视频，可以避免退出页面时视频画面残留
    if (alpha >= 1 && !this->video_stopped) {
#ifdef BOREALIS_USE_D3D11
        ID3D11Texture2D *tex = nullptr;
        auto swapChain = D3D11_CONTEXT->getSwapChain();
        swapChain->GetBuffer(0, IID_PPV_ARGS(&tex));
        mpv_fbo.tex = tex;
#elif defined(BOREALIS_USE_DEKO3D)
        static auto videoContext =
            dynamic_cast<brls::SwitchVideoContext *>(brls::Application::getPlatform()->getVideoContext());
        this->mpv_fbo.tex = videoContext->getFramebuffer();
        videoContext->queueSignalFence(&readyFence);
        videoContext->queueFlush();
#endif
        // 绘制视频
#ifdef PS5_NATIVE_GPU
        {
            // Use the SDL owner's explicit linear target. Do not cache the
            // window's framebuffer 0 at player initialization or render PQ UI
            // directly over encoded video. Restore this owner after mpv.
            default_framebuffer = brls::Application::getPlatform()->getVideoContext()->getLinearHdrFramebuffer();
            if (!default_framebuffer) {
                brls::fatal("native HDR: player has no linear composition target");
            }
            {
                // Direct (window) or Composite (PQ layer); NanoVG keeps the linear target below.
                uint32_t videoTarget = 0;
                int videoFormat = 0;
                if (!brls::Application::getPlatform()->getVideoContext()->selectHdrVideoTarget(videoTarget, videoFormat))
                    brls::fatal("native HDR: player has no video target");
                mpv_fbo.fbo = videoTarget;
                mpv_fbo.internal_format = videoFormat;
            }
            ps5_native_heap_checkpoint(Ps5HeapPhase::RenderBegin);
            mpv_render_context_render(this->mpv_context, mpv_params);
            ps5_native_heap_checkpoint(Ps5HeapPhase::RenderEnd);
        }
#else
        mpv_render_context_render(this->mpv_context, mpv_params);
#endif
#ifdef BOREALIS_USE_D3D11
        tex->Release();
        D3D11_CONTEXT->beginFrame();
#elif defined(BOREALIS_USE_DEKO3D)
        videoContext->queueWaitFence(&doneFence);
#elif defined(BOREALIS_USE_OPENGL)
        glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
        glViewport(0, 0, brls::Application::windowWidth, brls::Application::windowHeight);
#endif
#ifdef PS5_NATIVE_GPU
        presentationPending = true;
#else
        mpv_render_context_report_swap(this->mpv_context);
#endif
    }
#endif
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
#ifdef PS5_NATIVE_GPU
    if (!callbacks || !mpv) return;
    auto session = callbacks->gate;
    while (mpv && session->isActive()) {
#else
    while (true) {
#endif
        mpv_event *event = mpv_wait_event(this->mpv, 0);
#ifdef PS5_NATIVE_GPU
        // Nested event dispatch restores its caller's immutable snapshot. The
        // gate identity also rejects that snapshot if a callback replaces mpv.
        struct RestorePlaybackDispatch {
            MPVCore& core;
            PlaybackEvent previous;
            const CallbackGate* owner;
            ~RestorePlaybackDispatch() {
                core.playbackDispatchState = previous;
                core.playbackDispatchOwner = owner;
            }
        } restore{*this, playbackDispatchState, playbackDispatchOwner};
        playbackDispatchState = playbackState;
        playbackDispatchOwner = session.get();
#endif
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_LOG_MESSAGE: {
            auto log = reinterpret_cast<mpv_event_log_message *>(event->data);
            std::string text = log->text;
            while (!text.empty() && text.back() == '\n') text.pop_back();
            if (log->log_level <= MPV_LOG_LEVEL_ERROR) {
                brls::Logger::error("{}: {}", log->prefix, text);
            } else if (log->log_level <= MPV_LOG_LEVEL_WARN) {
                brls::Logger::warning("{}: {}", log->prefix, text);
            } else if (log->log_level <= MPV_LOG_LEVEL_INFO) {
                brls::Logger::info("{}: {}", log->prefix, text);
            } else if (log->log_level <= MPV_LOG_LEVEL_V) {
                brls::Logger::debug("{}: {}", log->prefix, text);
            } else {
                brls::Logger::verbose("{}: {}", log->prefix, text);
            }
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            brls::Logger::debug("MPVCore => EVENT_SHUTDOWN");
            return;
        case MPV_EVENT_FILE_LOADED:
#ifdef PS5_NATIVE_GPU
            ps5_native_heap_checkpoint(Ps5HeapPhase::FileLoaded);
#endif
            brls::Logger::info("MPVCore => EVENT_FILE_LOADED");
            // event 8: 文件预加载结束，准备解码
            mpvCoreEvent.fire(MpvEventEnum::MPV_LOADED);
            break;
        case MPV_EVENT_START_FILE:
#ifdef PS5_NATIVE_GPU
            playbackState = {event->data ? static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id : -1,
                false, false};
            playbackDispatchState = playbackState;
            ps5_native_heap_checkpoint(Ps5HeapPhase::StartFile);
#endif
            // event 6: 开始加载文件
            brls::Logger::info("MPVCore => EVENT_START_FILE");
#ifdef PS5_NATIVE_GPU
            // A new observer epoch forces initial values even if the next
            // file's codec/geometry equals the previous file's values. Merely
            // clearing a cache can lose those unchanged observations forever.
            this->observeProfileProperties();
#endif
            mpvCoreEvent.fire(MpvEventEnum::START_FILE);
#ifdef PS5_NATIVE_GPU
            if (!session->isActive()) return;
#endif
            mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
#ifdef PS5_NATIVE_GPU
            playbackState.started = playbackState.entry >= 0;
            playbackDispatchState = playbackState;
            playbackDispatchState.restarted = true;
#endif
            // event 21: 开始播放文件（一般是播放或调整进度结束之后触发）
            brls::Logger::info("MPVCore => EVENT_PLAYBACK_RESTART");
            this->video_stopped = false;
            if (this->isPaused())
                mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
            else
                mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
            break;
        case MPV_EVENT_END_FILE: {
#ifdef PS5_NATIVE_GPU
            ps5_native_heap_checkpoint(Ps5HeapPhase::EndFile);
#endif
            // event 7: 文件播放结束
            this->video_stopped = true;
#ifdef PS5_NATIVE_GPU
            this->clearProfileProperties();
#endif
            auto node = (mpv_event_end_file *)event->data;
#ifdef PS5_NATIVE_GPU
            // Pinned libmpv supplies mpv_event_end_file data for END_FILE.
            playbackDispatchState = {node->playlist_entry_id, false, false};
            playbackState = {};
#endif
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
#ifdef PS5_NATIVE_GPU
            const auto userdata = event->reply_userdata;
            int64_t entry = -1;
#else
            mpv_event_command *cmd = (mpv_event_command *)event->data;
#endif
            if (event->error) {
#ifdef PS5_NATIVE_GPU
                brls::Logger::error("MPVCore => COMMAND ERROR: {}", event->error);
            } else if (userdata > 0 && event->data) {
                const auto *cmd = static_cast<const mpv_event_command *>(event->data);
                if (cmd->result.format == MPV_FORMAT_NODE_MAP && cmd->result.u.list) {
                    const auto *list = cmd->result.u.list;
                    bool found = false;
                    if (list->num > 0 && list->keys && list->values) {
                        for (int i = 0; i < list->num; ++i) {
                            if (!list->keys[i]) { entry = -1; break; }
                            if (std::string(list->keys[i]) != "playlist_entry_id") continue;
                            // Duplicate keys are malformed, even when their values agree.
                            if (found) { entry = -1; break; }
                            found = true;
                            const auto &value = list->values[i];
                            if (value.format == MPV_FORMAT_INT64 && value.u.int64 >= 0)
                                entry = value.u.int64;
                        }
#else
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
#endif
                    }
                }
#ifdef PS5_NATIVE_GPU
            }
            // Consume all borrowed event storage before notifying the owner: a
            // subscriber may destroy the player or fetch another mpv event.
            // Failure also retires the owner's pending metadata exactly once.
            if (userdata > 0) {
                this->mpvCommandReply.fire(userdata, entry);
                if (!session->isActive()) return;
#endif
            }
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE: {
            /// https://mpv.io/manual/stable/#property-list
            mpv_event_property *prop = (mpv_event_property *)event->data;
#ifdef PS5_NATIVE_GPU
            if (event->reply_userdata == 110) {
                if (prop->format == MPV_FORMAT_STRING && prop->data)
                    brls::Logger::info("mpv: active {}={}", prop->name, *static_cast<char**>(prop->data));
                break;
            }
            if (event->reply_userdata & (uint64_t{1} << 63)) {
                cacheProfileProperty(profileProperties, event->reply_userdata, *prop);
                break;
            }
#endif
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
                this->video_paused = *(int *)prop->data;
                if (this->video_paused) {
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
#ifdef PS5_NATIVE_GPU
            case 6: {  // idle-active: distinct from buffering/core-idle.
                if (prop->format == MPV_FORMAT_FLAG && prop->data && *static_cast<int*>(prop->data)) {
                    mpvCoreEvent.fire(MpvEventEnum::MPV_IDLE);
                    if (!session->isActive()) return;
                }
                break;
            }
#endif
            case 9:  // speed
                this->video_speed = *(double *)prop->data;
                mpvCoreEvent.fire(VIDEO_SPEED_CHANGE);
                break;
#ifdef PS5_NATIVE_GPU
            case 10: {  // volume
                // Event storage belongs to mpv and can disappear if a listener
                // restarts the player. Copy it before firing application events.
                int64_t previous = this->volume;
                this->volume = *(int64_t *)prop->data;
                if (this->volume > 0 && previous == 0) {
#else
            case 10:  // volume
                if (*(int64_t *)prop->data > 0 && this->volume == 0) {
#endif
                    mpvCoreEvent.fire(VIDEO_UNMUTE);
#ifdef PS5_NATIVE_GPU
                } else if (this->volume == 0 && previous > 0) {
#else
                } else if (*(int64_t *)prop->data == 0 && this->volume > 0) {
#endif
                    mpvCoreEvent.fire(VIDEO_MUTE);
                }
#ifdef PS5_NATIVE_GPU
#else
                this->volume = *(int64_t *)prop->data;
#endif
                break;
#ifdef PS5_NATIVE_GPU
            }
#endif
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
#ifdef PS5_NATIVE_GPU
    this->clearProfileProperties();
#endif
    mpvCoreEvent.fire(MpvEventEnum::RESET);
    this->stop();
    this->video_stopped = true;
    this->video_paused = false;
    this->duration = 0;     // second
    this->cache_speed = 0;  // Bps
    this->playback_time = 0;
    this->video_progress = 0;

    setFrameSize(rect);
}

#ifdef PS5_NATIVE_GPU
bool MPVCore::setNativeAudioChannels(const std::string& preference) {
    const std::string selected = ps5_audio_policy::surround(PS5_NATIVE_AUDIO_51_AVAILABLE != 0, preference)
        ? "5.1" : "stereo";
    if (selected == AUDIO_CHANNELS) return true;
    // mpv's UPDATE_AUDIO option path rebuilds only its audio output/filter
    // chain. Restarting the player here would destroy a background music queue.
    if (mpv && !applyNativeAudioPolicy(mpv, true, selected)) {
        brls::Logger::error("ps5: audio preference update failed");
        if (!applyNativeAudioPolicy(mpv, true, AUDIO_CHANNELS))
            brls::Logger::error("ps5: audio preference restore failed");
        return false;
    }
    AUDIO_CHANNELS = selected;
    return true;
}

int MPVCore::setUrl(const std::string &url, const std::string &extra, const std::string &method, uint64_t userdata,
    int64_t sourceWidth, int64_t sourceHeight) {
    if (!mpv) return MPV_ERROR_UNINITIALIZED;
    std::lock_guard<std::mutex> playlistLock(playlistCommandMutex);
    ++playlistMutationGeneration;
    brls::Logger::debug("MPVCore load method={}", method);
    brls::Logger::info("mpv: submitting media to player");
    ps5_native_heap_checkpoint(Ps5HeapPhase::LoadBefore);
    const auto nativeExtra = ps5_native_cache_budget::loadOptions(
        ps5_native_decoder_budget::loadOptions(extra, nativeDecoderThreads, sourceWidth, sourceHeight),
        nativeCacheLimits);
    ps5_native_startup::detail::line("DECODER-LOAD schema=1 threads=%lld source-w=%lld source-h=%lld\n",
        static_cast<long long>(ps5_native_decoder_budget::selectForSource(nativeDecoderThreads, sourceWidth,
            sourceHeight)), static_cast<long long>(sourceWidth), static_cast<long long>(sourceHeight));
    int result;
#else
void MPVCore::setUrl(const std::string &url, const std::string &extra, const std::string &method, uint64_t userdata) {
    brls::Logger::debug("MPVCore {} ({}) extra: ({})", method, url, extra);
#endif
    if (mpv_client_api_version() >= MPV_MAKE_VERSION(2, 3)) {
#ifdef PS5_NATIVE_GPU
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), "0", nativeExtra.c_str(), nullptr};
        result = mpv_command_async(this->mpv, userdata, cmd);
#else
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), "0", extra.c_str(), nullptr};
        mpv_command_async(this->mpv, userdata, cmd);
#endif
    } else {
#ifdef PS5_NATIVE_GPU
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), nativeExtra.c_str(), nullptr};
        result = mpv_command_async(this->mpv, userdata, cmd);
#else
        const char *cmd[] = {"loadfile", url.c_str(), method.c_str(), extra.c_str(), nullptr};
        mpv_command_async(this->mpv, userdata, cmd);
#endif
    }
#ifdef PS5_NATIVE_GPU
    ps5_native_heap_checkpoint(Ps5HeapPhase::LoadAfter);
    return result;
#endif
}

void MPVCore::togglePlay() { this->command("cycle", "pause"); }
#ifdef PS5_NATIVE_GPU

int MPVCore::playPlaylistIndex(size_t index, int64_t expectedEntry, uint64_t expectedGeneration) {
    std::lock_guard<std::mutex> playlistLock(playlistCommandMutex);
    if (!mpv) return MPV_ERROR_UNINITIALIZED;
    if (playlistMutationGeneration != expectedGeneration || expectedEntry < 0) return MPV_ERROR_COMMAND;
    // Keep C++ playlist submissions out of the identity-check/selection gap.
    // Arbitrary external mpv scripts are not covered by this ownership guard.
    const auto entryProperty = "playlist/" + std::to_string(index) + "/id";
    int64_t entry = -1;
    const int identified = mpv_get_property(mpv, entryProperty.c_str(), MPV_FORMAT_INT64, &entry);
    if (identified < 0) return identified;
    if (entry != expectedEntry) return MPV_ERROR_COMMAND;
    const auto position = std::to_string(index);
    const char* cmd[] = {"playlist-play-index", position.c_str(), nullptr};
    const int result = mpv_command(mpv, cmd);
    if (result < 0) return result;
    // mpv accepts an out-of-range index as a stop. Confirm the documented
    // idle-active property, rather than mistaking command acceptance for start.
    int idle = 1;
    const int observed = mpv_get_property(mpv, "idle-active", MPV_FORMAT_FLAG, &idle);
    if (observed < 0) return observed;
    return idle ? MPV_ERROR_COMMAND : result;
}
#endif

void MPVCore::stop() { this->command("stop"); }

void MPVCore::seek(double value, const std::string &flags) {
    std::string pos = std::to_string(value);
    this->command("seek", pos.c_str(), flags.c_str());
}

bool MPVCore::isStopped() const { return video_stopped; }

bool MPVCore::isPaused() { return video_paused; }

double MPVCore::getSpeed() const { return video_speed; }

void MPVCore::setSpeed(double value) {
    std::string speed = std::to_string(value);
    this->command("set", "speed", speed.c_str());
}

void MPVCore::enableVO(bool value) { mpv_set_option_string(mpv, "vo", value ? MPVCore::VO.c_str() : "null"); }

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

#ifdef PS5_NATIVE_GPU
void MPVCore::applySubtitlePreferences() {
    auto& conf = AppConfig::instance();
    const auto changes = subtitlePreferences.update(
        conf.getItem(AppConfig::PS5_SUBTITLE_SIZE, 0), conf.getItem(AppConfig::PS5_SUBTITLE_MARGIN, -1));
    if (changes.fontSize) this->setDouble("sub-font-size", *changes.fontSize);
    if (changes.marginY) this->setInt("sub-margin-y", *changes.marginY);
}

void MPVCore::clearProfileProperties() {
    if (mpv && profileObservationId) mpv_unobserve_property(mpv, profileObservationId);
    profileObservationId = 0;
    profileProperties.reset();
}

void MPVCore::observeProfileProperties() {
    this->clearProfileProperties();
    if (!mpv) return;
    profileObservationId = profileProperties.reset();
    check_error(mpv_observe_property(mpv, profileObservationId, "seekable", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, profileObservationId, "demuxer-cache-state", MPV_FORMAT_NODE));
    // Observations are evaluated asynchronously by mpv. Keeping this small
    // scalar snapshot warm lets both the profile view read without
    // waiting for playback, including their first visible frame.
    for (const char* property : {"path", "file-format", "playlist-path", "video-format", "video-codec",
             "video-params/pixelformat", "hwdec-current", "audio-codec",
             "current-tracks/sub/title", "current-tracks/sub/codec"}) {
        check_error(mpv_observe_property(mpv, profileObservationId, property, MPV_FORMAT_STRING));
#else
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
#endif
    }
#ifdef PS5_NATIVE_GPU
    for (const char* property : {"file-size", "video-params/w", "video-params/h",
             "video-bitrate", "decoder-frame-drop-count", "frame-drop-count",
             "audio-params/channel-count", "audio-params/samplerate", "audio-bitrate", "sid"}) {
        check_error(mpv_observe_property(mpv, profileObservationId, property, MPV_FORMAT_INT64));
    }
    for (const char* property : {"container-fps", "estimated-vf-fps", "avsync", "demuxer-cache-duration"}) {
        check_error(mpv_observe_property(mpv, profileObservationId, property, MPV_FORMAT_DOUBLE));
    }

}
#else
    return nodeMap;
}
#endif
