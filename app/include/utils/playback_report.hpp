#pragma once

#include "api/jellyfin/media.hpp"

namespace jellyfin {

inline uint64_t playbackTicks(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return 0;
    const long double ticks = static_cast<long double>(seconds) * PLAYTICKS;
    const auto maximum = std::numeric_limits<int64_t>::max();
    return ticks >= static_cast<long double>(maximum) ? static_cast<uint64_t>(maximum) : static_cast<uint64_t>(ticks);
}

// A reporting identity is immutable between bind() and stop(). Buffering and
// seek restarts do not create extra starts; stop is emitted at most once.
class PlaybackReport {
public:
    void bind(std::string item, std::string source, std::string session, std::string method) {
        identity = {{"ItemId", std::move(item)}, {"MediaSourceId", std::move(source)},
            {"PlaySessionId", std::move(session)}, {"PlayMethod", std::move(method)}};
        started = false;
    }
    bool active() const { return started; }
    std::optional<nlohmann::json> start(double seconds, bool paused, bool canSeek) {
        if (started || identity.empty() || identity.value("ItemId", std::string()).empty()) return {};
        started = true;
        return sample(seconds, paused, canSeek);
    }
    std::optional<nlohmann::json> progress(double seconds, bool paused, bool canSeek) const {
        if (!started) return {};
        return sample(seconds, paused, canSeek);
    }
    std::optional<nlohmann::json> stop(double seconds, bool paused, bool canSeek) {
        if (!started) return {};
        auto result = sample(seconds, paused, canSeek);
        started = false;
        identity = {};
        return result;
    }
private:
    nlohmann::json identity;
    bool started = false;
    nlohmann::json sample(double seconds, bool paused, bool canSeek) const {
        auto result = identity;
        result["PositionTicks"] = playbackTicks(seconds);
        result["IsPaused"] = paused;
        result["CanSeek"] = canSeek;
        return result;
    }
};

} // namespace jellyfin
