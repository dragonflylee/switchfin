#pragma once

#include <cstdint>

namespace ps5::playback {

// PlaybackInfo completions and begin() run on the UI thread. The view's
// existing ASYNC_TOKEN handles destruction; this handles superseded requests
// while that same view is still alive (quality changes and next/previous).
class LatestRequest {
public:
    uint64_t begin() {
        pending = true;
        return ++generation;
    }

    bool finish(uint64_t ticket) {
        if (!pending || ticket != generation) return false;
        pending = false;
        return true;
    }

private:
    uint64_t generation = 0;
    bool pending = false;
};

}  // namespace ps5::playback
