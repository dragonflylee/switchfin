cmake_minimum_required(VERSION 3.6)

project(jni)

# armeabi-v7a requires cpufeatures library
# include(AndroidNdkModules)
# android_ndk_import_module_cpufeatures()

set(MPV_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/src/jniLibs/${ANDROID_ABI}")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${MPV_PREFIX})
set(ENV{PKG_CONFIG_LIBDIR} ${MPV_PREFIX}/lib/pkgconfig)
set(CMAKE_PREFIX_PATH ${MPV_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(APP_PLATFORM_LIB
    ${MPV_PREFIX}/lib/libmpv.so
    ${MPV_PREFIX}/lib/libavcodec.so
    ${MPV_PREFIX}/lib/libavdevice.so
    ${MPV_PREFIX}/lib/libavfilter.so
    ${MPV_PREFIX}/lib/libavformat.so
    ${MPV_PREFIX}/lib/libswresample.so
    ${MPV_PREFIX}/lib/libswscale.so
    ${MPV_PREFIX}/lib/libavutil.so
)

set(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/src/library/borealis/libromfs-generator" CACHE STRING "")
if (NOT EXISTS "${LIBROMFS_PREBUILT_GENERATOR}")
    message(FATAL_ERROR "libromfs-generator has not been built, please refer to borealis/build_libromfs_generator.sh for more information")
endif()

# app option
set(PLATFORM_ANDROID ON CACHE BOOL "")
set(USE_SYSTEM_CURL ON CACHE BOOL "")
set(USE_SYSTEM_SDL2 ON CACHE BOOL "")
set(USE_GLES3 ON CACHE BOOL "")
set(SIMPLE_HIGHLIGHT ON CACHE BOOL "")

# https://github.com/curl/curl/issues/12086
# if (ANDROID_ABI STREQUAL armeabi-v7a)
#     set(HAVE_FSEEKO 0)
# endif()
add_subdirectory(src)
