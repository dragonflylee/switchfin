#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

// Bound demux queues independently of decoder threads and the shared UI heap.
// These limits bound retained packets, not the total playback allocation.
namespace ps5_native_cache_budget {
inline constexpr std::int64_t maximumForward = 8 * 1024 * 1024;
inline constexpr std::int64_t maximumBackward = 2 * 1024 * 1024;
struct Limits {
    std::int64_t forward = maximumForward;
    std::int64_t backward = maximumBackward;
};

template <class Read, class Write>
inline bool configure(Read read, Write write, Limits& selected) {
    Limits next;
    struct Option {
        const char* name;
        const char* property;
        std::int64_t maximum;
        std::int64_t* selected;
        bool flag;
    };
    std::int64_t donation = 0;
    const Option options[] = {
        {"demuxer-max-bytes", "options/demuxer-max-bytes", maximumForward, &next.forward, false},
        {"demuxer-max-back-bytes", "options/demuxer-max-back-bytes", maximumBackward, &next.backward, false},
        {"demuxer-donate-buffer", "options/demuxer-donate-buffer", 0, &donation, true},
    };
    for (const auto& option : options) {
        std::int64_t requested = 0;
        if (!read(option.property, requested, option.flag)) return false;
        const auto bounded = std::clamp(requested, std::int64_t{0}, option.maximum);
        if (!write(option.name, bounded, option.flag)) return false;
        std::int64_t observed = -1;
        if (!read(option.property, observed, option.flag) || observed != bounded) return false;
        *option.selected = bounded;
    }
    selected = next;
    return true;
}

inline std::string loadOptions(const std::string& extra, const Limits& selected) {
    // Pinned mpv applies these after profiles/resume options, in list order.
    // Preserve lower user limits and append the budget after caller options.
    return extra + (extra.empty() ? "" : ",") + "demuxer-max-bytes=" +
        std::to_string(std::clamp(selected.forward, std::int64_t{0}, maximumForward)) +
        ",demuxer-max-back-bytes=" +
        std::to_string(std::clamp(selected.backward, std::int64_t{0}, maximumBackward)) +
        ",demuxer-donate-buffer=no";
}
} // namespace ps5_native_cache_budget
