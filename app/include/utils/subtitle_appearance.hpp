#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace subtitle_appearance {

// Zero/-1 mean the user's mpv configuration, not a replacement default.
inline constexpr std::array<int, 11> sizes{0, 50, 75, 90, 100, 110, 125, 150, 175, 200, 250};
inline constexpr std::array<int, 6> margins{-1, 22, 40, 60, 90, 150};

template <std::size_t N>
inline int indexOf(const std::array<int, N>& choices, int value) {
    const auto found = std::find(choices.begin(), choices.end(), value);
    return found == choices.end() ? 0 : static_cast<int>(found - choices.begin());
}

struct Changes {
    std::optional<double> fontSize;
    std::optional<int64_t> marginY;
};

// The host captures these after mpv.conf is loaded. Keep options that affect
// authored ASS (sub-scale, sub-pos, sub-ass-override, etc.) under mpv's control.
// mpv's sub-font-size/sub-margin-y apply to converted text, unless the user
// explicitly chose force/strip ASS overrides in their own configuration.
class Preferences {
public:
    Preferences(double fontSize = 55, int64_t marginY = 22)
        : originalFontSize(fontSize), originalMarginY(marginY) {}

    Changes update(int sizePercent, int marginY) {
        Changes changes;
        const bool resize = indexOf(sizes, sizePercent) != 0;
        const bool reposition = indexOf(margins, marginY) != 0;
        if (resize) changes.fontSize = originalFontSize * sizePercent / 100.0;
        else if (sizeManaged) changes.fontSize = originalFontSize;
        if (reposition) changes.marginY = marginY;
        else if (marginManaged) changes.marginY = originalMarginY;
        sizeManaged = resize;
        marginManaged = reposition;
        return changes;
    }

private:
    double originalFontSize;
    int64_t originalMarginY;
    bool sizeManaged = false;
    bool marginManaged = false;
};
} // namespace subtitle_appearance
