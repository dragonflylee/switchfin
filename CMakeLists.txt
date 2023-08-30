cmake_minimum_required(VERSION 3.15)

# build options
set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis/library)

# Include Platform common options
# example: cmake -B build -DPLATFORM_DESKTOP=ON
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SYSTEM_CURL "use system installed libcurl" ON)

# Linux
# Using CMAKE_INSTALL_PREFIX specifies the location where the application is installed.
cmake_dependent_option(CMAKE_INSTALL "Install to system path" OFF "UNIX;NOT APPLE" OFF)

# NintendoSwitch
cmake_dependent_option(BUILTIN_NSP "Built in NSP forwarder" OFF "PLATFORM_SWITCH" OFF)

# mpv related
# If your system does not support OpenGL(ES), you can use software rendering, but it will affect performance.
option(MPV_SW_RENDER "Using CPU to draw videos" OFF)
if (MPV_SW_RENDER)
    list(APPEND APP_PLATFORM_OPTION -DMPV_SW_RENDER)
endif ()

# On systems that do not support framebuffer, let MPV to draw to full screen and
# then cover unnecessary areas with UI.
option(MPV_NO_FB "Using system provided framebuffer" OFF)
if (MPV_NO_FB)
    list(APPEND APP_PLATFORM_OPTION -DMPV_NO_FB)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake)

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(Switchfin)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "2")
set(VERSION_ALTER "0")
set(PROJECT_TITLEID "010ff000ffff0003")
set(PROJECT_AUTHOR "dragonflylee")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

if (CUSTOM_RESOURCES_DIR)
    set(BRLS_RESOURCES_DIR ${CUSTOM_RESOURCES_DIR})
elseif (CMAKE_INSTALL)
    set(BRLS_RESOURCES_DIR ${CMAKE_INSTALL_PREFIX}/share)
endif ()

# setting src and include
file(GLOB_RECURSE MAIN_SRC "${PROJECT_SOURCE_DIR}/app/src/*.cpp")
if (PLATFORM_SWITCH)
    # System service initialization functions required for libnx
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
elseif (WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/app/app_win32.rc.in" "${CMAKE_BINARY_DIR}/app_win32.rc")
    list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/app_win32.rc)
elseif (APPLE)
    list(APPEND MAIN_SRC ${CMAKE_SOURCE_DIR}/app/app.icns)
endif ()

if (USE_LIBROMFS)
    message(STATUS "USE LIBROMFS")
    add_definitions(-DUSE_LIBROMFS)
    set(LIBROMFS_PROJECT_NAME ${PROJECT_NAME})
    set(LIBROMFS_RESOURCE_LOCATION "${PROJECT_RESOURCES}")
endif ()

# build borealis and other third party libraries
add_subdirectory(library)

if (USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
else ()
    include(FetchContent)
    FetchContent_Declare(curl
        URL https://github.com/curl/curl/releases/download/curl-7_88_1/curl-7.88.1.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if (WIN32)
        set(CURL_USE_SCHANNEL ON CACHE INTERNAL "" FORCE)
    else ()
        find_package(OpenSSL REQUIRED)
        set(CURL_USE_OPENSSL ON CACHE INTERNAL "" FORCE)
    endif()
    set(HTTP_ONLY ON CACHE INTERNAL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "" FORCE)
    set(BUILD_TESTING OFF CACHE INTERNAL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE INTERNAL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE INTERNAL "" FORCE)
    set(CURL_DISABLE_PROGRESS_METER ON CACHE INTERNAL "" FORCE)
    FetchContent_MakeAvailable(curl)
endif ()
list(APPEND APP_PLATFORM_LIB CURL::libcurl)

find_package(WebP)
if (WebP_FOUND)
    list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
    list(APPEND APP_PLATFORM_INCLUDE ${WebP_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${WebP_LIBRARY})
endif ()

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

if (PLATFORM_DESKTOP)
    find_package(MPV)
    if (MPV_FOUND)
        list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
        list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
    elseif (MINGW)
        include(ExternalProject)
        ExternalProject_Add(mpv
            URL https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20230831/mpv-dev-x86_64-20230831-git-edd7189.7z
            URL_HASH SHA256=1d6b964e97faecad291fe5cc128a3ccf04138d3a0b2ff0635290060ebaf98d34
            UPDATE_COMMAND ""
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            BUILD_IN_SOURCE TRUE
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/libmpv-2.dll ${CMAKE_BINARY_DIR}
        )
        ExternalProject_Get_property(mpv SOURCE_DIR)
        add_dependencies(borealis mpv)
        list(APPEND APP_PLATFORM_INCLUDE ${SOURCE_DIR}/include)
        list(APPEND APP_PLATFORM_LIB ${SOURCE_DIR}/libmpv.dll.a)
    else ()
        message(FATAL_ERROR "Missing library MPV")
    endif ()
    if (MINGW)
        list(APPEND APP_PLATFORM_LIB "-static-libgcc -static-libstdc++")
    endif ()
else ()
    # Used for Cross Compile such as NintendoSwitch
    # If dependent files cannot be found, environment variables can be set before executing the cmake command:
    # PKG_CONFIG_LIBDIR=/some/path/arm64/lib/pkgconfig
    find_package(PkgConfig REQUIRED)

    pkg_search_module(MPV REQUIRED mpv)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${MPV_STATIC_LIBRARIES} )
    list(APPEND APP_PLATFORM_LINK_OPTION -L${MPV_LIBRARY_DIRS})
endif()

if (USE_BOOST_FILESYSTEM)
    find_package(Boost 1.44 REQUIRED COMPONENTS filesystem)
    if(Boost_FOUND)
        list(APPEND APP_PLATFORM_LIB Boost::filesystem)
        list(APPEND APP_PLATFORM_OPTION -DUSE_BOOST_FILESYSTEM)
    endif()
endif ()

if (BUILTIN_NSP)
    list(APPEND APP_PLATFORM_LIB nsp)
    list(APPEND APP_PLATFORM_OPTION -DBUILTIN_NSP)
    add_subdirectory(scripts/forwarder)
endif ()

# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE app/include ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})
target_link_options(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LINK_OPTION})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION=v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}
    BUILD_TAG_VERSION=${GIT_TAG_VERSION}
    BUILD_TAG_SHORT=${GIT_TAG_SHORT}
    BUILD_PACKAGE_NAME=${CMAKE_PROJECT_NAME}
)

# building release file
if (PLATFORM_DESKTOP)
    if (APPLE)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures" FORCE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11" CACHE STRING "" FORCE)
        set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "11.0" CACHE STRING "arm 64 minimum deployment target" FORCE)
        message(STATUS "CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")

        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}"
            MACOSX_BUNDLE_ICON_FILE "app.icns"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2023 ${PROJECT_AUTHOR}"
            RESOURCE "${CMAKE_SOURCE_DIR}/app/app.icns"
        )

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} Resources/resources
            COMMAND dylibbundler -cd -b -x MacOS/${PROJECT_NAME} -d Library/ -p @executable_path/../Library/
            COMMAND codesign --sign - --force Library/*
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.app/Contents"
        )
    elseif (CMAKE_INSTALL)
        install(TARGETS ${PROJECT_NAME})
        install(DIRECTORY ${PROJECT_RESOURCES} DESTINATION ${BRLS_RESOURCES_DIR}
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        )
    elseif (NOT USE_LIBROMFS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${PROJECT_BINARY_DIR}/resources
        )
    endif()

elseif(PLATFORM_SWITCH)
    if (BUILTIN_NSP)
        add_dependencies(${PROJECT_NAME} forwarder.nsp)
    endif ()
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
endif ()