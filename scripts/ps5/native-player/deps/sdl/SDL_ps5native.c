/* PS5 OpenGL - SDL2 video driver for the native EGL runtime.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted for Switchfin native services and failure recovery.
 * Built inside SDL2; public SDL APIs and the upstream event queue are unchanged.
 */
#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"
#include "events/SDL_keyboard_c.h"
#include "video/ps5/SDL_ps5keyboard.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "ps5_display.h"

/* The video thread owns the fixed window/context and EGL presentation. */
typedef struct {
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    SDL_Window *window;
    SDL_threadID thread;
    int interval;
} NativeVideo;

/* SDL frees its video device even when its void quit callback fails.
 * Retain the ownership record until the original thread can release it. */
static NativeVideo *pending_cleanup;

static int egl_error(const char *operation)
{
    return SDL_SetError("NativeVideo %s: EGL error 0x%04x", operation, eglGetError());
}

static int on_thread(NativeVideo *g)
{
    return SDL_ThreadID() == g->thread ? 0 :
        SDL_SetError("NativeVideo requires the video thread");
}

static int video_init(_THIS)
{
    NativeVideo *g = _this->driverdata;
    int index;
    SDL_DisplayMode mode = { SDL_PIXELFORMAT_ABGR8888, PS5_OPENGL_NATIVE_WIDTH,
                            PS5_OPENGL_NATIVE_HEIGHT, PS5_OPENGL_NATIVE_FPS, NULL };
    g->thread = SDL_ThreadID();
    g->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g->display == EGL_NO_DISPLAY) return egl_error("eglGetDisplay");
    if (!eglInitialize(g->display, NULL, NULL)) {
        g->display = EGL_NO_DISPLAY;
        return egl_error("eglInitialize");
    }
    /* Physical keyboard availability must not disable controller UI. */
    if (PS5_Keyboard_Init() < 0 || PS5_Keyboard_Open() < 0)
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "%s", SDL_GetError());
    index = SDL_AddBasicVideoDisplay(&mode);
    if (index < 0) return -1;
    return SDL_AddDisplayMode(&_this->displays[index], &mode) ? 0 : -1;
}

static int load_library(_THIS, const char *path)
{
    (void)_this;
    return path ? SDL_SetError("NativeVideo is statically linked; use a NULL GL library path") : 0;
}

static void *get_proc(_THIS, const char *name)
{
    void *proc;
    (void)_this;
    if (!name || !*name) {
        SDL_SetError("NativeVideo requires a GL procedure name");
        return NULL;
    }
    proc = (void *)eglGetProcAddress(name);
    if (!proc) SDL_SetError("NativeVideo has no GL procedure %s", name);
    return proc;
}

static int create_window(_THIS, SDL_Window *window)
{
    NativeVideo *g = _this->driverdata;
    EGLint count = 0, width = 0, height = 0;
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, _this->gl_config.red_size,
        EGL_GREEN_SIZE, _this->gl_config.green_size,
        EGL_BLUE_SIZE, _this->gl_config.blue_size,
        EGL_ALPHA_SIZE, _this->gl_config.alpha_size,
        EGL_DEPTH_SIZE, _this->gl_config.depth_size,
        EGL_STENCIL_SIZE, _this->gl_config.stencil_size, EGL_NONE
    };
    if (on_thread(g) < 0) return -1;
    if (g->window || g->surface || window->w != PS5_OPENGL_NATIVE_WIDTH ||
        window->h != PS5_OPENGL_NATIVE_HEIGHT ||
        !(window->flags & SDL_WINDOW_OPENGL) || (window->flags & SDL_WINDOW_RESIZABLE))
        return SDL_SetError("NativeVideo requires one fixed %dx%d OpenGL window",
                            PS5_OPENGL_NATIVE_WIDTH, PS5_OPENGL_NATIVE_HEIGHT);
    if (_this->gl_config.stereo || _this->gl_config.multisamplebuffers ||
        _this->gl_config.multisamplesamples || _this->gl_config.floatbuffers ||
        _this->gl_config.framebuffer_srgb_capable || _this->gl_config.accum_red_size ||
        _this->gl_config.accum_green_size || _this->gl_config.accum_blue_size ||
        _this->gl_config.accum_alpha_size || _this->gl_config.buffer_size > 32 ||
        !_this->gl_config.double_buffer)
        return SDL_SetError("NativeVideo SDL supports double-buffered RGBA8 without MSAA, stereo, accumulation or sRGB");
    if (!eglChooseConfig(g->display, attributes, &g->config, 1, &count))
        return egl_error("eglChooseConfig");
    if (count != 1) return SDL_SetError("NativeVideo has no matching EGL config");
    g->surface = eglCreateWindowSurface(g->display, g->config, (EGLNativeWindowType)0, NULL);
    if (!g->surface) return egl_error("eglCreateWindowSurface");
    /* Establish ownership before queries, so SDL's failed-create cleanup works. */
    g->window = window;
    if (!eglQuerySurface(g->display, g->surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(g->display, g->surface, EGL_HEIGHT, &height))
        return egl_error("eglQuerySurface");
    if (width != window->w || height != window->h)
        return SDL_SetError("NativeVideo EGL drawable %dx%d does not match the requested %dx%d window",
                            width, height, window->w, window->h);
    g->interval = 1;
    SDL_SetKeyboardFocus(window); /* SDL also uses focus to gate joystick events. */
    return 0;
}

static int make_current(_THIS, SDL_Window *window, SDL_GLContext context)
{
    NativeVideo *g = _this->driverdata;
    EGLSurface surface = context ? g->surface : EGL_NO_SURFACE;
    if (on_thread(g) < 0) return -1;
    if (context && (context != g->context || window != g->window))
        return SDL_SetError("NativeVideo context/window mismatch");
    if (!eglMakeCurrent(g->display, surface, surface, (EGLContext)context))
        return egl_error("eglMakeCurrent");
    return 0;
}

static void delete_context(_THIS, SDL_GLContext context)
{
    NativeVideo *g = _this->driverdata;
    if (on_thread(g) < 0 || !context) return;
    if (context != g->context) {
        SDL_SetError("NativeVideo unknown context");
        return;
    }
    /* SDL_GL_DeleteContext detaches first and updates SDL's TLS. If that
     * failed, EGL rejects destruction; keep both ownership records intact. */
    if (!eglDestroyContext(g->display, g->context)) {
        egl_error("eglDestroyContext");
        return;
    }
    g->context = EGL_NO_CONTEXT;
}

static SDL_GLContext create_context(_THIS, SDL_Window *window)
{
    NativeVideo *g = _this->driverdata;
    const EGLint attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE
    };
    if (on_thread(g) < 0) return NULL;
    if (g->context || window != g->window ||
        _this->gl_config.major_version != 3 || _this->gl_config.minor_version != 3 ||
        _this->gl_config.profile_mask != SDL_GL_CONTEXT_PROFILE_CORE ||
        _this->gl_config.flags || _this->gl_config.share_with_current_context ||
        _this->gl_config.no_error || _this->gl_config.reset_notification ||
        _this->gl_config.release_behavior != SDL_GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH) {
        SDL_SetError("NativeVideo SDL requires one unshared OpenGL 3.3 Core context with default flags");
        return NULL;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        egl_error("eglBindAPI");
        return NULL;
    }
    g->context = eglCreateContext(g->display, g->config, EGL_NO_CONTEXT, attributes);
    if (!g->context) {
        egl_error("eglCreateContext");
        return NULL;
    }
    if (make_current(_this, window, g->context) < 0) {
        delete_context(_this, g->context);
        return NULL;
    }
    return (SDL_GLContext)g->context;
}

static int set_interval(_THIS, int interval)
{
    NativeVideo *g = _this->driverdata;
    if (on_thread(g) < 0) return -1;
    if (interval != 0 && interval != 1) return SDL_SetError("NativeVideo swap interval must be 0 or 1");
    if (!eglSwapInterval(g->display, interval)) return egl_error("eglSwapInterval");
    g->interval = interval;
    return 0;
}

static int get_interval(_THIS) { return ((NativeVideo *)_this->driverdata)->interval; }

static int swap_window(_THIS, SDL_Window *window)
{
    NativeVideo *g = _this->driverdata;
    if (on_thread(g) < 0) return -1;
    if (window != g->window) return SDL_SetError("NativeVideo unknown window");
    return eglSwapBuffers(g->display, g->surface) ? 0 : egl_error("eglSwapBuffers");
}

static void destroy_window(_THIS, SDL_Window *window)
{
    NativeVideo *g = _this->driverdata;
    if (g->window != window) return; /* Includes rejected second-window creation. */
    if (on_thread(g) < 0) return;
    g->window = NULL;
    /* SDL contexts outlive windows; release the drawable, keep the context. */
    if (make_current(_this, NULL, NULL) < 0) return;
    if (!eglDestroySurface(g->display, g->surface)) {
        egl_error("eglDestroySurface");
        return;
    }
    g->surface = EGL_NO_SURFACE;
}

static void set_window_size(_THIS, SDL_Window *window)
{
    (void)_this;
    window->w = window->windowed.w = PS5_OPENGL_NATIVE_WIDTH;
    window->h = window->windowed.h = PS5_OPENGL_NATIVE_HEIGHT;
    SDL_SetError("NativeVideo SDL does not support resizing");
}

static void video_quit(_THIS)
{
    NativeVideo *g = _this->driverdata;
    if (on_thread(g) < 0 || !g->display) return;
    if (PS5_Keyboard_Close() < 0) return;
    delete_context(_this, g->context);
    if (g->context) return; /* Never release resources still owned by EGL. */
    if (g->surface) {
        if (!eglDestroySurface(g->display, g->surface)) {
            egl_error("eglDestroySurface(quit)");
            return;
        }
        g->surface = EGL_NO_SURFACE;
    }
    if (!eglTerminate(g->display)) {
        egl_error("eglTerminate");
        return;
    }
    g->display = EGL_NO_DISPLAY;
}

static void free_device(_THIS)
{
    NativeVideo *g = _this->driverdata;
    if (g->display) {
        pending_cleanup = g;
        if (!*SDL_GetError()) SDL_SetError("NativeVideo cleanup incomplete");
    } else {
        SDL_free(g);
    }
    SDL_free(_this);
}

/* Required SDL callback; joystick polling follows this in SDL_PumpEvents. */
static void pump_events(_THIS) { (void)_this; PS5_Keyboard_PumpEvents(); }

static SDL_VideoDevice *create_device(void)
{
    SDL_VideoDevice *device;
    if (pending_cleanup) {
        SDL_VideoDevice retired;
        NativeVideo *g = pending_cleanup;
        if (on_thread(g) < 0) return NULL;
        SDL_zero(retired);
        retired.driverdata = g;
        if (g->context && !eglMakeCurrent(g->display, EGL_NO_SURFACE,
                                            EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
            egl_error("eglMakeCurrent(cleanup)");
            return NULL;
        }
        video_quit(&retired);
        if (g->display) return NULL;
        SDL_free(g);
        pending_cleanup = NULL;
    }
    device = SDL_calloc(1, sizeof(*device));
    if (!device) { SDL_OutOfMemory(); return NULL; }
    device->driverdata = SDL_calloc(1, sizeof(NativeVideo));
    if (!device->driverdata) { SDL_free(device); SDL_OutOfMemory(); return NULL; }
    device->VideoInit = video_init;
    device->VideoQuit = video_quit;
    device->PumpEvents = pump_events;
    device->HasScreenKeyboardSupport = PS5_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = PS5_ShowScreenKeyboard;
    device->HideScreenKeyboard = PS5_HideScreenKeyboard;
    device->IsScreenKeyboardShown = PS5_IsScreenKeyboardShown;
    device->CreateSDLWindow = create_window;
    device->DestroyWindow = destroy_window;
    device->SetWindowSize = set_window_size;
    device->GL_LoadLibrary = load_library;
    device->GL_GetProcAddress = get_proc;
    device->GL_CreateContext = create_context;
    device->GL_MakeCurrent = make_current;
    device->GL_DeleteContext = delete_context;
    device->GL_SetSwapInterval = set_interval;
    device->GL_GetSwapInterval = get_interval;
    device->GL_SwapWindow = swap_window;
    device->free = free_device;
    /* SDL_PumpEvents already updates the upstream joystick backend. No extra
     * polling thread or replacement event queue is required by this consumer. */
    return device;
}

VideoBootStrap PS5_bootstrap = { "ps5-native", "PS5 frozen NativeVideo EGL", create_device, NULL };
