#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiPlayback = "{}/Items/{}/PlaybackInfo?{}";

struct MediaStream {
    std::string Codec;
    std::string DisplayTitle;
    std::string Type;
    long Index;
    bool IsDefault;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaStream, Codec, DisplayTitle, Type, Index, IsDefault);

struct MediaSource {
    std::string Id;
    std::string Name;
    std::string ETag;
    time_t RunTimeTicks;
    std::vector<MediaStream> MediaStreams;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaSource, Id, Name, ETag, RunTimeTicks, MediaStreams);


struct PlaybackResult {
    std::vector<MediaSource> MediaSources;
    std::string PlaySessionId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlaybackResult, MediaSources, PlaySessionId);

}