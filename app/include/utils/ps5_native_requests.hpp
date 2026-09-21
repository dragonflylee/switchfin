#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

// Native server-page requests. Scope creation, admission, close and drain are
// UI-thread operations. Workers own value snapshots and never access a View.
namespace ps5_native_requests {
using Cancel = std::shared_ptr<std::atomic_bool>;
using Clock = std::chrono::steady_clock;

class DeadlineExceeded final : public std::exception {
public:
    const char* what() const noexcept override {
        return "Server request timed out. You can try again or go back.";
    }
};

namespace detail {
struct Owner { bool active = true; }; // Accessed only by the UI thread.

inline void reportFailure() noexcept {
    static unsigned reports = 0;
    if (reports == 16) return;
    ++reports;
    try { brls::Logger::error("ps5 native: request callback or admission failed"); }
    catch (...) {}
}

struct Job {
    explicit Job(std::shared_ptr<Owner> owner)
        : owner(std::move(owner)), cancelled(std::make_shared<std::atomic_bool>(false)) {}
    virtual ~Job() = default;
    virtual void run(const Cancel&) noexcept = 0;
    virtual void deliver() = 0;
    virtual void deliverDeadline() = 0;
    virtual void discard() noexcept = 0;

    void block() {
        // Application increments before firing hints/logging, which may throw.
        // Record ownership first so that either path removes exactly one token.
        inputHeld = true;
        brls::Application::blockInputs();
    }
    void releaseInput() noexcept {
        if (!inputHeld) return;
        inputHeld = false;
        try { brls::Application::unblockInputs(); }
        catch (...) {} // Its token decrement occurs before hints/logging.
    }
    void cancelOnUi() noexcept {
        cancelled->store(true);
        releaseInput();
        discard();
    }

    std::shared_ptr<Owner> owner;
    // HTTP may retain its cancel option until reuse/cleanup. That reference
    // must own only the flag, never the response, callback or whole request.
    Cancel cancelled;
    std::atomic_bool ready{false};
    bool inputHeld = false;
    Clock::time_point deadline = Clock::time_point::max(); // UI thread only.
};

// Keep cancelled/in-flight work counted until the worker acknowledges it.
// Repeated page destruction cannot create an unbounded background backlog.
inline std::array<std::shared_ptr<Job>, 32> pending;
inline bool draining = false;

template <typename Work, typename Done>
struct Request final : Job {
    using Result = std::invoke_result_t<Work&, const Cancel&>;
    static_assert(!std::is_void_v<Result>, "Requests return an owned value");
    Request(std::shared_ptr<Owner> owner, Work work, Done done, std::chrono::milliseconds maxWait)
        : Job(std::move(owner)), work(std::move(work)), done(std::move(done)) {
        if (maxWait.count() > 0) {
            // Prepare the fixed failure at admission, never allocate it while
            // restoring input. Cap addition to keep the clock arithmetic bounded.
            deadlineFailure = std::make_exception_ptr(DeadlineExceeded{});
            const auto limit = std::chrono::milliseconds(60000);
            deadline = Clock::now() + (maxWait < limit ? maxWait : limit);
        }
    }

    void run(const Cancel& cancel) noexcept override {
        try {
            if (!cancelled->load()) result.emplace((*work)(cancel));
        } catch (...) { failure = std::current_exception(); }
        work.reset();
        // Publishes result/failure and releases all worker-only input captures.
        ready.store(true, std::memory_order_release);
    }
    void deliver() override {
        if (done) (*done)(result ? &*result : nullptr, failure);
    }
    void deliverDeadline() override {
        // The worker may still be writing result/failure: do not touch either.
        // Unlike ready delivery, this job is still pending. Page destruction
        // can therefore find it reentrantly; defer discarding the active lambda.
        deliveringDeadline = true;
        struct Guard { bool& active; ~Guard() { active = false; } } guard{deliveringDeadline};
        if (done) (*done)(nullptr, deadlineFailure);
    }
    void discard() noexcept override { if (!deliveringDeadline) done.reset(); }

    std::optional<Work> work;
    std::optional<Done> done; // Never read or destroyed by the worker.
    std::optional<Result> result;
    std::exception_ptr failure;
    std::exception_ptr deadlineFailure; // UI-owned; independent of worker failure.
    bool deliveringDeadline = false; // UI thread only.
};
} // namespace detail

class Scope {
public:
    Scope() : owner(std::make_shared<detail::Owner>()) {}
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { close(); }

    void close() noexcept {
        if (!owner->active) return;
        auto state = owner;
        state->active = false;
        // Snapshot shared ownership before releasing inputs: hint listeners can
        // reenter UI code. No iterator or callback into a destroyed page survives.
        auto batch = detail::pending;
        for (auto& job : batch)
            if (job && job->owner == state) job->cancelOnUi();
    }

    template <typename Work, typename Done>
    bool start(Work work, Done done, bool blockInputs = false,
               std::chrono::milliseconds maxWait = std::chrono::milliseconds::zero()) noexcept {
        if (!owner->active) return false;
        auto state = owner;
        std::shared_ptr<detail::Job> job;
        std::size_t slot = detail::pending.size();
        try {
            for (std::size_t i = 0; i < detail::pending.size(); ++i)
                if (!detail::pending[i]) { slot = i; break; }
            if (slot == detail::pending.size()) return false;
            using Task = detail::Request<Work, Done>;
            job = std::make_shared<Task>(state, std::move(work), std::move(done), maxWait);
            detail::pending[slot] = job;
            if (blockInputs) job->block();
            if (!state->active) {
                detail::pending[slot].reset();
                job->cancelOnUi();
                return false;
            }
            brls::async([job] {
                job->run(job->cancelled);
            });
            return true;
        } catch (...) {
            if (job) {
                if (detail::pending[slot] == job) detail::pending[slot].reset();
                job->cancelOnUi();
            }
            detail::reportFailure();
            return false;
        }
    }

private:
    std::shared_ptr<detail::Owner> owner;
};

inline void drain(Clock::time_point now = Clock::now()) noexcept {
    if (detail::draining) return;
    detail::draining = true;
    auto batch = detail::pending;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        auto& job = batch[i];
        if (!job || detail::pending[i] != job) continue;
        if (!job->ready.load(std::memory_order_acquire)) {
            if (now < job->deadline || job->cancelled->load()) continue;
            // A queued/stuck worker cannot hold the UI indefinitely. Keep its
            // occupied slot and worker ownership until acknowledgement; cancel
            // and deliver only the independent deadline failure on the UI thread.
            job->cancelled->store(true);
            job->releaseInput();
            if (job->owner->active) {
                try { job->deliverDeadline(); }
                catch (...) { detail::reportFailure(); }
            }
            job->discard();
            continue;
        }
        detail::pending[i].reset();
        job->releaseInput();
        // Recheck after hint listeners run: releasing input can close the page.
        if (job->owner->active && !job->cancelled->load()) {
            try { job->deliver(); }
            catch (...) { detail::reportFailure(); }
        }
        job->discard();
    }
    detail::draining = false;
}
} // namespace ps5_native_requests
