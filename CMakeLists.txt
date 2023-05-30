cmake_minimum_required(VERSION 3.15)

# build options
set(PLATFORM_DESKTOP OFF CACHE BOOL "build for desktop, ON for desktop; OFF for switch")
set(PLATFORM_SWITCH OFF CACHE BOOL "build for NintendoSwitch")
set(WIN32_TERMINAL ON CACHE BOOL "Show terminal when run on Windows")

# mpv related
set(MPV_SW_RENDER OFF CACHE BOOL "Using CPU to draw videos")

option(USE_GLFW "using glfw for input and create window" ON)
option(USE_SDL2 "using sdl2 for input and create window" OFF)

if(USE_SDL2)
    message("SDL2")
    set(USE_GLFW OFF)
elseif(USE_GLFW)
    message("GLFW")
    set(USE_SDL2 OFF)
endif()

if (MPV_SW_RENDER)
    add_definitions(-DMPV_SW_RENDER)
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake)

# toolchain
if (PLATFORM_DESKTOP)
    message("building for Desktop")
    set(TARGET_PLATFORM desktop CACHE STRING "")
    message("Build Type: ${CMAKE_BUILD_TYPE}")
    SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g2 -ggdb -Wall")
    SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
elseif (PLATFORM_SWITCH)
    message("building for SWITCH")
    message("Build Type: ${CMAKE_BUILD_TYPE}")
    set(CMAKE_USE_SYSTEM_ENVIRONMENT_PATH OFF)
    file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" DEVKITPRO)
    if (NOT IS_DIRECTORY ${DEVKITPRO})
        message(FATAL_ERROR "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
    endif ()
    set(DEVKITPRO $ENV{DEVKITPRO} CACHE BOOL "DEVKITPRO")
    include(${DEVKITPRO}/cmake/Switch.cmake REQUIRED)
    set(__SWITCH__ ON)
    set(CMAKE_C_FLAGS "-I${DEVKITPRO}/libnx/include -I${DEVKITPRO}/portlibs/switch/include")
    set(CMAKE_CXX_FLAGS "-I${DEVKITPRO}/libnx/include -I${DEVKITPRO}/portlibs/switch/include")
else()
    message(FATAL_ERROR "Please set build target -DPLATFORM_DESKTOP=ON or -DPLATFORM_SWITCH=ON")
endif ()

# project info
project(Switchfin)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_TITLEID "0x010FF000FFFF0003")
set(PROJECT_AUTHOR "dragonflylee")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
add_definitions(-DBUILD_PACKAGE_NAME=${CMAKE_PROJECT_NAME})

# build borealis qrcode and other third party libraries
add_subdirectory(library)

# setting src and include
file(GLOB_RECURSE MAIN_SRC "${CMAKE_SOURCE_DIR}/app/src/*.cpp")

set(PLATFORM_OPTION)
if (PLATFORM_DESKTOP)
    find_package(CURL REQUIRED)
    find_package(MPV REQUIRED)
    if (MPV_FOUND)
        message(STATUS "Found libmpv: ${MPV_VERSION} ${MPV_INCLUDE_DIR} ${MPV_LIBRARY}")
    endif()
    set(PLATFORM_LIBS CURL::libcurl ${MPV_LIBRARY})
elseif (PLATFORM_SWITCH)
    set(PLATFORM_LIBS
            # needed by borealis
            glfw3 EGL glapi drm_nouveau
            # needed by mpv
            mpv swscale swresample avformat avfilter avcodec avutil ass freetype fribidi png bz2 z SDL2
            mbedx509 mbedtls mbedcrypto
            # needed by curl
            curl z
            # base lib
            nx m
            )
    list(APPEND MAIN_SRC ${CMAKE_SOURCE_DIR}/library/borealis/library/lib/platforms/switch/switch_wrapper.c)
endif()

# building target
if (WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/app/app_win32.rc.in" "${CMAKE_BINARY_DIR}/app_win32.rc")
    if (WIN32_TERMINAL)
        add_executable(${PROJECT_NAME} ${MAIN_SRC} "${CMAKE_BINARY_DIR}/app_win32.rc")
    else ()
        add_executable(${PROJECT_NAME} WIN32 ${MAIN_SRC} "${CMAKE_BINARY_DIR}/app_win32.rc")
    endif ()
else ()
    add_executable(${PROJECT_NAME} ${MAIN_SRC})
endif ()
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE app/include)
target_compile_options(${PROJECT_NAME} PRIVATE
        -ffunction-sections
        -fdata-sections
        ${PLATFORM_OPTION}
        )
target_link_libraries(${PROJECT_NAME} PRIVATE
        borealis
        lunasvg
        ${PLATFORM_LIBS}
        )


# building release file
if (PLATFORM_DESKTOP)
    add_custom_target(${PROJECT_NAME}.data
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${CMAKE_SOURCE_DIR}/resources ${CMAKE_CURRENT_BINARY_DIR}/resources
            )
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
elseif(PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
            COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp --titleid=${PROJECT_TITLEID}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
            COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
            )
endif ()