#pragma once

#include <string>
#include <stdint.h>
#include <borealis/core/event.hpp>

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
typedef brls::Event<uint64_t, int64_t> MPVCommandReply;

// 用于 VideoView 可以接收的自定义事件
const std::string VIDEO_CLOSE = "VIDEO_CLOSE";
// 用于 PlayerView 可以接收的自定义事件
const std::string QUALITY_CHANGE = "QUALITY_CHANGE";

const std::string TRACK_START = "TRACK_START";
