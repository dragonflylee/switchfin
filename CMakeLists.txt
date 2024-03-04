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

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
elseif (CMAKE_BUILD_TYPE STREQUAL Debug)
    list(APPEND APP_PLATFORM_OPTION -D_DEBUG -D_GLIBCXX_ASSERTIONS)
endif()

# Add git info
if (NOT DEFINED GIT_TAG_SHORT)
    execute_process(COMMAND git rev-parse --short HEAD
        TIMEOUT 5 OUTPUT_VARIABLE GIT_TAG_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

if (NOT DEFINED VERSION_BUILD)
    execute_process(COMMAND git rev-list --count --all
        TIMEOUT 5 OUTPUT_VARIABLE VERSION_BUILD
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()
message(STATUS "git commit ${GIT_TAG_SHORT} build ${VERSION_BUILD}")

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(Switchfin)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "4")
set(VERSION_ALTER "4")
set(PROJECT_TITLEID "010ff000ffff0003")
set(PROJECT_AUTHOR "dragonflylee")
set(PACKAGE_NAME "org.player.switchfin")
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

if (MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
endif ()

if (USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
else ()
    include(FetchContent)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.6.0.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
        set(USE_WIN32_IDN ON)
        set(ZLIB_USE_STATIC_LIBS ON)
    else ()
        find_package(OpenSSL REQUIRED)
        set(CURL_USE_OPENSSL ON)
    endif()
    set(HTTP_ONLY ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(USE_LIBIDN2 OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_DISABLE_PROGRESS_METER ON)
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

if (USE_BOOST_FILESYSTEM)
    find_package(Boost 1.44 REQUIRED COMPONENTS filesystem)
    if(Boost_FOUND)
        list(APPEND APP_PLATFORM_LIB Boost::filesystem)
        list(APPEND APP_PLATFORM_OPTION -DUSE_BOOST_FILESYSTEM)
    endif()
endif ()

if (PLATFORM_DESKTOP)
    find_package(MPV REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
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

if (BUILTIN_NSP)
    list(APPEND APP_PLATFORM_LIB nsp)
    list(APPEND APP_PLATFORM_OPTION -DBUILTIN_NSP)
    add_subdirectory(scripts/forwarder)
endif ()

if (USE_DEKO3D)
    gen_dksh("${PROJECT_RESOURCES}/shaders")
endif ()

# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE app/include ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})
target_link_options(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LINK_OPTION})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}
    APP_BUILD=${VERSION_BUILD}
    BUILD_TAG_SHORT=${GIT_TAG_SHORT}
    BUILD_PACKAGE_NAME=${CMAKE_PROJECT_NAME}
)

# building release file
if (PLATFORM_DESKTOP)
    if (APPLE)
        message(STATUS "CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")

        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}"
            MACOSX_BUNDLE_ICON_FILE "app.icns"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2024 ${PROJECT_AUTHOR}"
            RESOURCE "${CMAKE_SOURCE_DIR}/app/app.icns"
        )

        if (CMAKE_PREFIX_PATH)
            set(BUNDLE_ARG -s ${CMAKE_PREFIX_PATH}/lib)
        endif ()

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} Resources/resources
            COMMAND otool -L MacOS/${PROJECT_NAME}
            COMMAND dylibbundler -cd -ns -b ${BUNDLE_ARG} -x MacOS/${PROJECT_NAME} -d Frameworks/ -p "@executable_path/../Frameworks/"
            COMMAND codesign --sign - --force Frameworks/*
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.app/Contents"
        )
    elseif (CMAKE_INSTALL)
        set(icon_dirs 32x32 48x48 64x64 128x128 256x256)
        linux_install(
            "${PROJECT_RESOURCES}" "${BRLS_RESOURCES_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.svg"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/icons" "${icon_dirs}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.appdata.xml"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.desktop"
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
