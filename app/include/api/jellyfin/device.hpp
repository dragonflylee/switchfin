#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

struct DirectPlayProfile {
    std::string Container;
    std::string Type;
    std::string VideoCodec;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DirectPlayProfile, Container, Type, VideoCodec);

struct TranscodingProfile {
    std::string Container;
    std::string Type;
    std::string VideoCodec;
    std::string Context;
    std::string Protocol;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TranscodingProfile, Container, Type, VideoCodec, Context, Protocol);

struct SubtitleProfile {
    std::string Format;
    std::string Method;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubtitleProfile, Format, Method);

struct DeviceProfile {
    int64_t MaxStreamingBitrate;
    int64_t MaxStaticBitrate;
    std::vector<DirectPlayProfile> DirectPlayProfiles;
    std::vector<TranscodingProfile> TranscodingProfiles;
    std::vector<SubtitleProfile> SubtitleProfiles;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DeviceProfile, MaxStreamingBitrate, MaxStaticBitrate, DirectPlayProfiles, TranscodingProfiles, SubtitleProfiles);

}  // namespace jellyfin