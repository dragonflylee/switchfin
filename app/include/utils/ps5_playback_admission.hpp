#pragma once

#include "api/jellyfin/media.hpp"
#include "utils/ps5_playback_profile.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ps5::playback {

enum class Admission { WithinPolicy, Unknown, Convert };

inline std::string normalized(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

inline bool hdr10Original(const jellyfin::Source& source) {
    unsigned videos = 0;
    if (source.IsInfiniteStream || source.Bitrate <= 0 || source.Bitrate > hdr10MaxBitrate) return false;
    for (const auto& stream : source.MediaStreams) {
        if (stream.Type != jellyfin::streamTypeVideo) continue;
        ++videos;
        const auto validRate = [](const std::optional<double>& rate) {
            return !rate || (std::isfinite(*rate) && *rate > 0 && *rate <= hdr10MaxFramerate);
        };
        if (normalized(stream.Codec) != "hevc" || normalized(stream.Profile) != "main 10" ||
            normalized(stream.VideoRangeType) != "hdr10" || normalized(stream.ColorTransfer) != "smpte2084" ||
            normalized(stream.ColorPrimaries) != "bt2020" || normalized(stream.ColorSpace) != "bt2020nc" ||
            !stream.Width || *stream.Width <= 0 || *stream.Width > int(hdr10MaxWidth) ||
            !stream.Height || *stream.Height <= 0 || *stream.Height > int(hdr10MaxHeight) ||
            !stream.BitDepth || *stream.BitDepth != 10 ||
            (!stream.AverageFrameRate && !stream.RealFrameRate) ||
            !validRate(stream.AverageFrameRate) || !validRate(stream.RealFrameRate)) return false;
    }
    return videos == 1;
}

// Evaluate ORIGINAL media only. PlaybackInfo's MediaStreams do not describe
// the encoded output of TranscodingUrl, so applying this to that output would
// incorrectly reject every successfully scaled/tone-mapped source.
inline Admission admitOriginal(const jellyfin::Source& source, Backend backend) {
    if (backend == Backend::Hdr10 && hdr10Original(source)) return Admission::WithinPolicy;
    const auto caps = capabilities(backend);
    bool unknown = false, video = false;
    for (const auto& stream : source.MediaStreams) {
        if (stream.Type != jellyfin::streamTypeVideo) continue;
        video = true;
        const auto codec = normalized(stream.Codec);
        if (!codec.empty() && codec != "h264" && codec != "hevc") return Admission::Convert;
        if ((stream.Width && *stream.Width > caps.maxInputWidth) ||
            (stream.Height && *stream.Height > caps.maxInputHeight) ||
            (stream.BitDepth && *stream.BitDepth > caps.maxInputBitDepth) ||
            (stream.AverageFrameRate && *stream.AverageFrameRate > caps.maxInputFramerate) ||
            (stream.RealFrameRate && *stream.RealFrameRate > caps.maxInputFramerate)) return Admission::Convert;
        const auto range = normalized(stream.VideoRangeType);
        const auto transfer = normalized(stream.ColorTransfer);
        if ((!range.empty() && range != "sdr" && range != "unknown") ||
            transfer == "smpte2084" || transfer == "arib-std-b67") return Admission::Convert;
        const bool knownSdr = range == "sdr" || transfer == "bt709" || transfer == "smpte170m" ||
            transfer == "iec61966-2-1";
        unknown |= codec.empty() || !stream.Width || !stream.Height || !stream.BitDepth ||
            (!stream.AverageFrameRate && !stream.RealFrameRate) || !knownSdr;
    }
    // Unknown originals require the explicit compatible request. Neither this
    // metadata envelope nor conversion proves total decoder-memory safety.
    return !video || unknown ? Admission::Unknown : Admission::WithinPolicy;
}

inline Admission admitOriginalDelivery(const jellyfin::Source& source, Backend backend, bool direct) {
    const auto admission = admitOriginal(source, backend);
    // No inferred HDR copy from SupportsDirectStream or a TranscodingUrl.
    return admission == Admission::WithinPolicy && hdr10Original(source) && !direct
        ? Admission::Convert : admission;
}

// Source geometry for the per-file decoder thread choice. Converted output is
// bounded by the 1080p transcoding profile, so it reports unknown (0, 0).
inline std::pair<int64_t, int64_t> originalVideoGeometry(const jellyfin::Source& source, bool converted) {
    if (converted) return {0, 0};
    std::pair<int64_t, int64_t> geometry{0, 0};
    for (const auto& stream : source.MediaStreams) {
        if (stream.Type != jellyfin::streamTypeVideo) continue;
        if (stream.Width && *stream.Width > geometry.first) geometry.first = *stream.Width;
        if (stream.Height && *stream.Height > geometry.second) geometry.second = *stream.Height;
    }
    return geometry;
}

inline void requestCompatiblePlayback(nlohmann::json& request) {
    request["EnableDirectPlay"] = false;
    request["EnableDirectStream"] = false;
    request["EnableTranscoding"] = true;
    request["AllowVideoStreamCopy"] = false;
    request["AllowAudioStreamCopy"] = false;
    request["MaxAudioChannels"] = 2;
    auto& profile = request["DeviceProfile"];
    // An HDR-capable direct profile must not leak HDR range conditions into
    // compatible retries. Those always encode H.264/AAC SDR with copying off.
    applyVideoPolicy(profile, Backend::SdrConversion, "h264");
    const auto limit = profile.value("MaxStreamingBitrate", int64_t{16000000});
    request["MaxStreamingBitrate"] = std::min<int64_t>(limit > 0 ? limit : 16000000, 16000000);
    profile["MaxStreamingBitrate"] = request["MaxStreamingBitrate"];
    for (auto& transcode : profile["TranscodingProfiles"]) {
        if (transcode.value("Type", std::string()) == "Video") {
            transcode["VideoCodec"] = "h264";
            transcode["AudioCodec"] = "aac";
        }
    }
}

class Fallback {
public:
    void reset(uint64_t ticks) { used = false; resumeTicks = ticks; }
    bool begin(std::optional<uint64_t> ticks = std::nullopt) {
        if (used) return false;
        used = true;
        if (ticks) resumeTicks = *ticks;
        return true;
    }
    bool active() const { return used; }
    uint64_t position() const { return resumeTicks; }
private:
    bool used = false;
    uint64_t resumeTicks = 0;
};

} // namespace ps5::playback
