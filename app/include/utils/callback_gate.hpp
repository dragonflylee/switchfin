#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

// One gate per player session. The scheduler may be called from any thread;
// dispatched work and close() execute on the UI thread. Closing a gate invalidates queued work
// without touching a newer session or retaining the player in the event queue.
class CallbackGate : public std::enable_shared_from_this<CallbackGate> {
public:
    enum class Channel { Events, Render };

    void close() { active.store(false); }
    bool isActive() const { return active.load(); }

    template <typename Schedule, typename Work>
    void post(Channel channel, Schedule&& schedule, Work&& work) {
        auto index = static_cast<unsigned>(channel);
        if (!active.load() || pending[index].exchange(true)) return;
        auto state = shared_from_this();
        try {
            schedule([state, index, work = std::forward<Work>(work)]() mutable {
                // Clear before calling work, so a notification during dispatch
                // schedules another drain instead of being lost.
                state->pending[index].store(false);
                if (state->active.load()) work();
            });
        } catch (...) {
            pending[index].store(false);
            throw;
        }
    }

private:
    std::atomic<bool> active{true};
    std::atomic<bool> pending[2]{{false}, {false}};
};
