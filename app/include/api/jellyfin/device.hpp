#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

struct SubtitleProfile {
    std::string Format;
    std::string Method;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubtitleProfile, Format, Method);

struct DeviceProfile {
    bool EnableDirectPlay;
    bool EnableTranscoding;
    std::vector<SubtitleProfile> SubtitleProfiles;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DeviceProfile, EnableDirectPlay);

}