#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>

namespace ps5::artwork {

enum class Delivery { Complete, Defer, Retry };

// UI-owned admission/delivery. Only an admitted Entry and its immutable job
// reference cross to a worker. Completion publication never allocates or calls
// UI code. A slot covers queued, running AND decoded/ready work until retirement.
// Pending metadata also has a finite count. This bounds job/pixel-buffer COUNT,
// not image size, decode scratch,
// GPU memory, total memory, wall time or the number of subscribers in a flight.
template <class Ref, std::size_t Capacity = 4, std::size_t ScanLimit = 64, std::size_t PendingLimit = 256>
class Scheduler {
    static_assert(Capacity > 0 && ScanLimit >= Capacity && PendingLimit > 0, "finite admission policy");
    enum class Phase : uint64_t { Queued, Running, Ready, Delivering, Retired, Waiting };
    static constexpr uint64_t phaseMask = 7;
    static Phase phaseOf(uint64_t state) noexcept { return static_cast<Phase>(state & phaseMask); }
    static uint64_t withPhase(uint64_t state, Phase phase) noexcept {
        return (state & ~phaseMask) | static_cast<uint64_t>(phase);
    }
    using Clock = std::chrono::steady_clock;
    struct Entry {
        const Ref job;
        // Tag every submitted attempt. A copied old task cannot claim Queued
        // again when a later retry reuses the same entry (phase ABA).
        std::atomic<uint64_t> state{static_cast<uint64_t>(Phase::Queued)};
        unsigned retries = 0; // UI-owned; at most two after the initial attempt.
        Clock::time_point retryAt{};
        bool failed = false; // Worker writes before publishing Ready.
        explicit Entry(Ref job) : job(std::move(job)) {}
    };
    using EntryRef = std::shared_ptr<Entry>;

public:
    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    struct Snapshot {
        std::size_t pending = 0, admitted = 0, ready = 0;
        std::size_t peakPending = 0, peakAdmitted = 0;
        std::size_t submissions = 0, completed = 0, cancelled = 0;
        std::size_t deliveryPasses = 0, deferred = 0;
        std::size_t pendingScans = 0, pendingPruned = 0;
        std::size_t submissionFailures = 0, workerFailures = 0, deliveryFailures = 0;
        std::size_t admissionRejected = 0, retries = 0, retriesExhausted = 0;
    };

    // If allocation fails, no worker was submitted and no entry was published.
    // The caller retires its UI registry request on false/exception.
    bool enqueue(const Ref& job) {
        if (closing || !job) return false;
        if (pending.size() >= PendingLimit) { ++totals.admissionRejected; return false; }
        pending.push_back(std::make_shared<Entry>(job));
        if (pending.size() > totals.peakPending) totals.peakPending = pending.size();
        return true;
    }

    // All callbacks except work execute on the UI thread. cancelled/retire must
    // not throw. retire releases the payload as well as registry membership,
    // before another request can be admitted into that slot. work must not use
    // the payload after returning; it may throw and will still publish Ready.
    // submit may allocate/throw; a late copied task is made inert when possible.
    // A bool delivery result of false keeps the slot/payload for a later pass;
    // void means finished. Cancellation/close always retires after the callback.
    template <class Submit, class Work, class Cancelled, class Deliver, class Retire>
    void pump(bool allowDelivery, Submit&& submit, Work work, Cancelled&& cancelled,
              Deliver&& deliver, Retire&& retire, Clock::time_point now = Clock::now()) {
        static_assert(noexcept(cancelled(std::declval<const Ref&>())), "cancel predicate must not throw");
        static_assert(noexcept(retire(std::declval<const Ref&>())), "retirement must not throw");
        if (closing || busy) return;
        struct Guard { bool& busy; Guard(bool& b) : busy(b) { busy = true; } ~Guard() { busy = false; } } guard(busy);

        auto submitEntry = [&](EntryRef& slot) {
            const EntryRef entry = slot;
            const uint64_t queuedState = entry->state.load(std::memory_order_acquire);
            try {
                submit([entry, work, queuedState](auto& resource) mutable {
                    auto expected = queuedState;
                    if (!entry->state.compare_exchange_strong(expected,
                            withPhase(queuedState, Phase::Running), std::memory_order_acq_rel)) return;
                    try { work(entry->job, resource); }
                    catch (...) { entry->failed = true; }
                    entry->state.store(withPhase(queuedState, Phase::Ready), std::memory_order_release);
                });
                ++totals.submissions;
            } catch (...) {
                ++totals.submissionFailures;
                auto expected = queuedState;
                if (entry->state.compare_exchange_strong(expected,
                        withPhase(queuedState, Phase::Retired), std::memory_order_acq_rel)) {
                    retire(entry->job);
                    slot.reset();
                }
                // A throwing submit may already have started its copied task.
                // Keep that slot until the worker publishes completion.
            }
        };

        // Cancelled pending jobs must retire even while every admission slot
        // is occupied by a slow worker. Scan once per run-loop delivery pass
        // and on a full-queue request/cancel pump, without an extra GPU delivery,
        // continuing across live entries so a live head cannot hide later
        // cancellations. Admission below has its own existing ScanLimit.
        if (allowDelivery || pending.size() >= PendingLimit) {
            const std::size_t count = pending.size() < ScanLimit ? pending.size() : ScanLimit;
            for (std::size_t scanned = 0; scanned < count && !pending.empty(); ++scanned) {
                if (pendingCursor == pending.end()) pendingCursor = pending.begin();
                auto cursor = pendingCursor++;
                ++totals.pendingScans;
                if (!cancelled((*cursor)->job)) continue;
                EntryRef entry = std::move(*cursor);
                pending.erase(cursor);
                ++totals.cancelled;
                ++totals.pendingPruned;
                retire(entry->job);
                if (closing) return;
            }
        }

        // Never touch a running worker's payload. Queued cancellation wins a
        // CAS against worker entry, so released slots cannot oversubscribe work.
        for (auto& entry : active) {
            if (!entry || !cancelled(entry->job)) continue;
            uint64_t state = entry->state.load(std::memory_order_acquire);
            if (phaseOf(state) == Phase::Queued) {
                entry->state.compare_exchange_strong(state, withPhase(state, Phase::Retired), std::memory_order_acq_rel);
                state = entry->state.load(std::memory_order_acquire);
            }
            if (phaseOf(state) == Phase::Running) continue;
            ++totals.cancelled;
            retire(entry->job);
            entry.reset();
        }

        // Round robin prevents a quick replacement in a low-numbered slot from
        // repeatedly overtaking ready older work in another slot.
        if (allowDelivery) {
            for (std::size_t offset = 0; offset < Capacity; ++offset) {
                const std::size_t slot = (nextDelivery + offset) % Capacity;
                auto& entry = active[slot];
                if (!entry || phaseOf(entry->state.load(std::memory_order_acquire)) != Phase::Ready) continue;
                // Keep the slot occupied during reentrant view callbacks and
                // retain a local reference if close() clears the active array.
                const EntryRef current = entry;
                const auto attemptState = current->state.load(std::memory_order_relaxed);
                current->state.store(withPhase(attemptState, Phase::Delivering), std::memory_order_release);
                bool finished = true, retry = false;
                ++totals.deliveryPasses;
                if (current->failed) ++totals.workerFailures;
                else {
                    try {
                        if constexpr (std::is_void_v<decltype(deliver(current->job))>) deliver(current->job);
                        else if constexpr (std::is_same_v<decltype(deliver(current->job)), Delivery>) {
                            const auto result = deliver(current->job);
                            retry = result == Delivery::Retry;
                            finished = result == Delivery::Complete;
                        } else finished = deliver(current->job);
                    }
                    catch (...) { ++totals.deliveryFailures; }
                }
                if (retry && current->retries == 2) {
                    ++totals.retriesExhausted;
                    finished = true;
                }
                if (finished || closing || cancelled(current->job)) {
                    retire(current->job);
                    entry.reset();
                    ++totals.completed;
                } else if (retry) {
                    current->retryAt = now + std::chrono::milliseconds(current->retries == 0 ? 100 : 400);
                    ++current->retries;
                    ++totals.retries;
                    current->state.store(withPhase(attemptState, Phase::Waiting), std::memory_order_release);
                } else {
                    ++totals.deferred;
                    current->state.store(withPhase(attemptState, Phase::Ready), std::memory_order_release);
                }
                nextDelivery = (slot + 1) % Capacity;
                break;
            }
        }

        if (closing) return;
        // A waiting request retains its slot, pixels/body and view pins. Retry
        // only on a run-loop pass; ordinary request pumps cannot cause bursts.
        if (allowDelivery) {
            for (auto& slot : active) {
                if (!slot) continue;
                const auto state = slot->state.load(std::memory_order_acquire);
                if (phaseOf(state) != Phase::Waiting || now < slot->retryAt) continue;
                slot->failed = false;
                slot->state.store(withPhase(state + phaseMask + 1, Phase::Queued), std::memory_order_release);
                submitEntry(slot);
                if (closing) return;
            }
        }
        std::size_t scanned = 0;
        for (auto& slot : active) {
            if (slot) continue;
            while (!pending.empty() && scanned < ScanLimit) {
                ++scanned;
                if (pendingCursor == pending.begin()) ++pendingCursor;
                EntryRef entry = std::move(pending.front());
                pending.pop_front();
                if (cancelled(entry->job)) {
                    ++totals.cancelled;
                    retire(entry->job);
                    continue;
                }
                slot = entry; // Own the slot before submitting a worker.
                updatePeak();
                submitEntry(slot);
                break; // At most one submit attempt per slot per pump.
            }
        }
    }

    template <class Retire>
    void close(Retire&& retire) noexcept {
        static_assert(noexcept(retire(std::declval<const Ref&>())), "retirement must not throw");
        if (closing) return;
        closing = true;
        for (const auto& entry : pending) retire(entry->job);
        pending.clear();
        pendingCursor = pending.end();
        for (auto& entry : active) {
            if (!entry) continue;
            uint64_t state = entry->state.load(std::memory_order_acquire);
            if (phaseOf(state) == Phase::Queued)
                entry->state.compare_exchange_strong(state, withPhase(state, Phase::Retired), std::memory_order_acq_rel);
            if (phaseOf(state) != Phase::Running && phaseOf(state) != Phase::Delivering) retire(entry->job);
            // Running workers retain their own reference. Their cancelled job
            // and payload retire there; workers never refer to this Scheduler.
            entry.reset();
        }
    }

    Snapshot snapshot() const noexcept {
        Snapshot result = totals;
        result.pending = pending.size();
        for (const auto& entry : active) {
            if (!entry) continue;
            ++result.admitted;
            if (phaseOf(entry->state.load(std::memory_order_acquire)) == Phase::Ready) ++result.ready;
        }
        return result;
    }
    bool isClosing() const noexcept { return closing; }

private:
    void updatePeak() noexcept {
        std::size_t count = 0;
        for (const auto& entry : active) count += bool(entry);
        if (count > totals.peakAdmitted) totals.peakAdmitted = count;
    }
    std::list<EntryRef> pending;
    typename std::list<EntryRef>::iterator pendingCursor = pending.end();
    std::array<EntryRef, Capacity> active{};
    std::size_t nextDelivery = 0;
    Snapshot totals;
    bool closing = false, busy = false;
};

} // namespace ps5::artwork
