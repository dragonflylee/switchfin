#pragma once

#include "utils/ps5_native_artwork_body.hpp"

namespace ps5::artwork {

inline constexpr size_t pixelImageLimit = 8 * 1024 * 1024;
inline constexpr size_t pixelResidentLimit = 16 * 1024 * 1024;

// Bound RGBA output before entering a decoder. Account for the source's 16-bit
// expansion too, even though the upload ultimately uses 8-bit RGBA. This is an
// image admission limit, not a bound on codec scratch or GPU texture storage.
inline size_t admittedPixelBytes(int width, int height, bool sixteenBit) noexcept {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels > pixelImageLimit / (sixteenBit ? 8 : 4)) return 0;
    return pixels * 4;
}

class PixelReservation {
public:
    PixelReservation(EncodedBudget& budget, size_t bytes) noexcept
        : budget(budget), reserved(bytes && budget.acquire(bytes) ? bytes : 0) {}
    ~PixelReservation() { if (reserved) budget.release(reserved); }
    PixelReservation(const PixelReservation&) = delete;
    PixelReservation& operator=(const PixelReservation&) = delete;
    explicit operator bool() const noexcept { return reserved != 0; }
private:
    EncodedBudget& budget;
    size_t reserved;
};

} // namespace ps5::artwork
