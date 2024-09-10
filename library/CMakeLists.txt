cmake_minimum_required(VERSION 3.15)

# add lunasvg
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(lunasvg EXCLUDE_FROM_ALL)

# add dacap
if (PLATFORM_DESKTOP)
    set(CLIP_EXAMPLES OFF)
    set(CLIP_TESTS OFF)
    add_subdirectory(clip EXCLUDE_FROM_ALL)
    target_include_directories(clip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif ()

# add borealis
add_subdirectory(borealis/library)

if (PLATFORM_PS4)
    list(APPEND APP_PLATFORM_LIB SceNet mbedcrypto mbedtls mbedx509)
    set(APP_PLATFORM_LIB ${APP_PLATFORM_LIB} PARENT_SCOPE)
endif ()