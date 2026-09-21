#pragma once

#include <cstdint>
#include <string>

// UI, demux, software decoding and audio share the application heap.
// Two frame threads bound retained decoder buffers for ordinary input.
namespace ps5_native_decoder_budget {
inline constexpr std::int64_t maximumThreads = 2;
inline constexpr std::int64_t select(std::int64_t requested) noexcept {
    return requested == 1 ? 1 : maximumThreads;
}

template <class Read, class Write>
inline bool configure(Read read, Write write, std::int64_t& selected) {
    std::int64_t requested = 0;
    if (!read("options/vd-lavc-threads", requested)) return false;
    const auto bounded = select(requested);
    if (!write("vd-lavc-threads", bounded)) return false;
    std::int64_t observed = 0;
    if (!read("options/vd-lavc-threads", observed) || observed != bounded) return false;
    selected = bounded;
    return true;
}

inline std::string loadOptions(const std::string& extra, std::int64_t selected) {
    // Last entry wins in pinned mpv's ordered per-file option application, after
    // automatic profiles and resume options. Do not alter the caller's extras.
    return extra + (extra.empty() ? "" : ",") + "vd-lavc-threads=" +
        std::to_string(select(selected));
}

// Admitted sources above 1440p use six frame threads and require the
// 1 GiB direct-memory application heap. An explicit
// one-thread preference still wins; every other source keeps the 2-thread cap.
inline constexpr std::int64_t uhdThreads = 6;
inline constexpr std::int64_t uhdAboveWidth = 2560;
inline constexpr std::int64_t uhdAboveHeight = 1440;
inline constexpr std::int64_t selectForSource(std::int64_t selected, std::int64_t width, std::int64_t height) noexcept {
    if (select(selected) == 1) return 1;
    return width > uhdAboveWidth || height > uhdAboveHeight ? uhdThreads : maximumThreads;
}

inline std::string loadOptions(const std::string& extra, std::int64_t selected, std::int64_t width,
    std::int64_t height) {
    return extra + (extra.empty() ? "" : ",") + "vd-lavc-threads=" +
        std::to_string(selectForSource(selected, width, height));
}
} // namespace ps5_native_decoder_budget
