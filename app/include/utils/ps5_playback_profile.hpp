#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

// Only PS5 callers apply this policy. It has no display/window dependency and
// does not replace libavcodec's decoders or the player's stream-copy handling.
namespace ps5::playback {

enum class Backend { SdrConversion, Hdr10 };

inline constexpr unsigned hdr10MaxFramerate = 30;
inline constexpr int64_t hdr10MaxBitrate = 16000000;
// HEVC Main10 HDR10 uses software decoding and the 1 GiB direct-memory heap.
// SDR and H.264 input retain the 1920x1080 limit.
inline constexpr unsigned hdr10MaxWidth = 3840;
inline constexpr unsigned hdr10MaxHeight = 2160;

struct Capabilities {
    const char* profileName;
    unsigned maxInputWidth;
    unsigned maxInputHeight;
    unsigned maxInputFramerate;
    unsigned maxInputBitDepth;
};

inline constexpr Capabilities capabilities(Backend backend) noexcept {
    return {backend == Backend::Hdr10 ? "Switchfin PS5 HDR10" : "Switchfin PS5 SDR",
        1920, 1080, 60, 10};
}

namespace detail {
inline nlohmann::json condition(const char* kind, const char* property, std::string value) {
    // Jellyfin ProfileCondition.Value is a string, including numeric limits.
    // Its ConditionProcessor allows unknown metadata when IsRequired=false;
    // this preserves unprobed/live content without claiming it was qualified.
    return {{"Condition", kind}, {"Property", property}, {"Value", std::move(value)}, {"IsRequired", false}};
}
} // namespace detail

inline nlohmann::json videoConditions(Backend backend) {
    const auto caps = capabilities(backend);
    return nlohmann::json::array({
        detail::condition("LessThanEqual", "Width", std::to_string(caps.maxInputWidth)),
        detail::condition("LessThanEqual", "Height", std::to_string(caps.maxInputHeight)),
        detail::condition("LessThanEqual", "VideoFramerate", std::to_string(caps.maxInputFramerate)),
        detail::condition("LessThanEqual", "VideoBitDepth", std::to_string(caps.maxInputBitDepth)),
        // There is no Jellyfin ColorTransfer/SupportsHDR ProfileCondition.
        // VideoRangeType SDR excludes known PQ/HDR10, HLG and Dolby Vision.
        detail::condition("Equals", "VideoRangeType", "SDR"),
    });
}

inline nlohmann::json codecProfiles(Backend backend) {
    // Range restrictions MUST also be codec-qualified. Jellyfin ignores
    // VideoRangeType in unqualified TranscodingProfile.Conditions, whereas
    // these profiles generate h264-rangetype=SDR and hevc-rangetype=SDR.
    const auto conditions = videoConditions(backend);
    auto hevc = conditions;
    if (backend == Backend::Hdr10) {
        hevc.back() = detail::condition("EqualsAny", "VideoRangeType", "SDR|HDR10");
        // Server conditions cannot bind size to range. Larger HEVC SDR offered
        // by this profile is still converted by the local original gate.
        hevc[0] = detail::condition("LessThanEqual", "Width", std::to_string(hdr10MaxWidth));
        hevc[1] = detail::condition("LessThanEqual", "Height", std::to_string(hdr10MaxHeight));
        // Server profile conditions cannot express transfer/primaries together.
        // The local original/route gate below is required before loading HDR.
        hevc.back()["IsRequired"] = true;
    }
    return nlohmann::json::array({
        {{"Type", "Video"}, {"Codec", "h264"}, {"Conditions", conditions}},
        {{"Type", "Video"}, {"Codec", "hevc"}, {"Conditions", hevc}},
    });
}

inline std::string transcodeVideoCodecs(std::string_view preferred) {
    // The existing PS5 direct-play policy admits H.264 and HEVC. Preserve both
    // stream-copy choices and prefer the user's supported encoder selection.
    // AV1/VP9 decode/performance is unqualified; MPEG-4 Part 2/MPEG-2 are not
    // supported Jellyfin HLS video encode choices. Never advertise them here.
    return preferred == "hevc" || preferred == "h265" ? "hevc,h264" : "h264,hevc";
}

inline void applyVideoPolicy(nlohmann::json& profile, Backend backend, std::string_view preferred) {
    profile["Name"] = capabilities(backend).profileName;
    for (auto& direct : profile["DirectPlayProfiles"]) {
        if (direct.value("Type", std::string()) == "Video") direct["VideoCodec"] = "h264,hevc";
    }
    auto codecs = nlohmann::json::array();
    for (const auto& existing : profile["CodecProfiles"]) {
        if (existing.value("Type", std::string()) != "Video") codecs.push_back(existing);
    }
    for (const auto& video : codecProfiles(backend)) codecs.push_back(video);
    profile["CodecProfiles"] = std::move(codecs);
    for (auto& transcode : profile["TranscodingProfiles"]) {
        if (transcode.value("Type", std::string()) == "Video") {
            transcode["VideoCodec"] = transcodeVideoCodecs(preferred);
            transcode["Conditions"] = videoConditions(backend);
        }
    }
    // Audio/subtitle profiles, containers, bitrates and stream-copy request
    // flags stay with the existing player. Server tone-mapping availability
    // remains a separate requirement for successful HDR-source conversion.
}

} // namespace ps5::playback
