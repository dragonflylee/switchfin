/* Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* Fixed render/presentation profile, not negotiated HDMI timing. */
#include <ps5_opengl_display.h>

#if !((PS5_OPENGL_NATIVE_WIDTH == 1920 && PS5_OPENGL_NATIVE_HEIGHT == 1080) || \
      (PS5_OPENGL_NATIVE_WIDTH == 2560 && PS5_OPENGL_NATIVE_HEIGHT == 1440) || \
      (PS5_OPENGL_NATIVE_WIDTH == 3840 && PS5_OPENGL_NATIVE_HEIGHT == 2160)) || \
    !(PS5_OPENGL_NATIVE_FPS == 60 || PS5_OPENGL_NATIVE_FPS == 120)
#error Unsupported PS5 OpenGL display profile
#endif
