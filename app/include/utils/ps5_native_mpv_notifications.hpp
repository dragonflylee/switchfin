#pragma once

#include <atomic>

// Per-session notification bits only. MPV owns its event/render data. Worker
// callbacks neither allocate nor touch the UI/player; the UI consumes once per
// frame. close() is terminal, including notifications racing with shutdown.
class Ps5NativeMpvNotifications {
public:
    enum : unsigned { Events = 1, Render = 2, Closed = 4 };
    void notify(unsigned bits) noexcept { state.fetch_or(bits & (Events | Render), std::memory_order_release); }
    unsigned take() noexcept {
        const unsigned value = state.fetch_and(Closed, std::memory_order_acq_rel);
        return value & Closed ? 0 : value & (Events | Render);
    }
    void close() noexcept { state.fetch_or(Closed, std::memory_order_acq_rel); }
private:
    static_assert(std::atomic<unsigned>::is_always_lock_free, "MPV notification callbacks must not lock");
    std::atomic<unsigned> state{0};
};
