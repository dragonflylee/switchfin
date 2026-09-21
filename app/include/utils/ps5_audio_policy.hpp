#pragma once

#include <string_view>

namespace ps5_audio_policy {
inline constexpr bool surround(bool available, std::string_view preference) noexcept {
    return available && preference == "5.1";
}

// Apply before mpv_initialize, after configuration-file loading, and on an
// explicit live preference change. Set the hint before mpv can reopen audio.
// A refused surround hint falls back to explicit stereo. If that hint also
// fails, leave the mpv option untouched and report failure to the caller.
template <class Hint, class Write>
inline bool configure(bool available, std::string_view preference, Hint hint, Write write) {
    bool selected = surround(available, preference);
    if (!hint(selected ? "1" : "0")) {
        if (!selected || !hint("0")) return false;
        selected = false;
    }
    return write(selected ? "5.1,stereo" : "stereo");
}
} // namespace ps5_audio_policy
