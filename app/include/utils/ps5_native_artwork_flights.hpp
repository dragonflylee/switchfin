#pragma once

#include <atomic>
#include <iterator>
#include <list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace ps5::artwork {

struct DemandLimits {
    static constexpr size_t requests = 260; // 256 pending plus four admitted.
    static constexpr size_t consumers = 512;
    static constexpr size_t consumersPerRequest = 128;
    static constexpr size_t keyBytes = 8 * 1024;
    static constexpr size_t retainedKeyBytes = 512 * 1024;
    static constexpr size_t headers = 16;
};

// All registry/member operations belong to the UI thread. Workers only use the
// immutable URL, shared cancellation flag and payload. An exact URL has one
// worker until completion is sealed; each subscribing view keeps its own pin.
// View pin/unpin operations must not throw, matching Borealis' view contract.
template <class View, class Payload, class Limits = DemandLimits>
class Flights {
    static_assert(Limits::requests > 0 && Limits::consumers > 0 && Limits::consumersPerRequest > 0 &&
        Limits::retainedKeyBytes >= Limits::keyBytes, "finite demand policy");
    // Admission is UI-owned. A worker may release the last request reference,
    // so credits are shared and atomic, independent of the registry's lifetime.
    // Charge sealed/cancelled requests until destruction, not lookup removal.
    struct Credits {
        std::atomic<size_t> requests{0}, bytes{0};
    };
    struct Lease {
        std::shared_ptr<Credits> credits;
        size_t bytes;
        Lease(std::shared_ptr<Credits> credits, size_t bytes) : credits(std::move(credits)), bytes(bytes) {
            this->credits->requests.fetch_add(1, std::memory_order_relaxed);
            this->credits->bytes.fetch_add(bytes, std::memory_order_relaxed);
        }
        Lease(Lease&& other) noexcept : credits(std::move(other.credits)), bytes(other.bytes) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() {
            if (!credits) return;
            credits->bytes.fetch_sub(bytes, std::memory_order_release);
            credits->requests.fetch_sub(1, std::memory_order_release);
        }
    };
public:
    Flights() = default;
    Flights(const Flights&) = delete;
    Flights& operator=(const Flights&) = delete;

    struct Request {
    private:
        Lease lease; // Destroy after the key, payload and members.
    public:
        const std::string url;
        const std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::atomic_bool completionFailed{false};
        Payload payload;

        Request(Lease lease, std::string url) : lease(std::move(lease)), url(std::move(url)) {}

    private:
        std::list<View*> members;
        bool sealed = false; // UI thread only; partial delivery may resume.
        friend class Flights;
    };
    using Ref = std::shared_ptr<Request>;

    Ref begin(View* view, const std::string& url, bool* start) {
        *start = false;
        cancel(view);
        if (closing) return {};
        if (!view || url.size() > Limits::keyBytes || active.size() >= Limits::consumers) return {};
        auto found = jobs.find(url);
        if (found != jobs.end() && found->second->completionFailed.load()) {
            // A worker cannot release UI pins after failed queue admission.
            // A later UI request retires that abandoned flight instead of
            // attaching to work that can no longer deliver a completion.
            const Ref failed = found->second;
            finish(failed);
            if (closing) return {};
            found = jobs.find(url);
        }
        const bool fresh = found == jobs.end();
        Ref job;
        if (fresh) {
            if (credits->requests.load(std::memory_order_acquire) >= Limits::requests ||
                url.size() > Limits::retainedKeyBytes - credits->bytes.load(std::memory_order_acquire)) return {};
            Lease lease(credits, url.size());
            job = std::make_shared<Request>(std::move(lease), url);
        } else {
            job = found->second;
            if (job->members.size() >= Limits::consumersPerRequest) return {};
        }
        job->members.push_back(view);
        const auto node = std::prev(job->members.end());
        try {
            const auto inserted = active.emplace(view, Consumer{job, node});
            if (!inserted.second) {
                job->members.erase(node);
                return {};
            }
            if (fresh) {
                try {
                    if (!jobs.emplace(url, job).second) {
                        active.erase(inserted.first);
                        job->members.erase(node);
                        return {};
                    }
                } catch (...) {
                    active.erase(inserted.first);
                    throw;
                }
            }
        } catch (...) {
            job->members.erase(node);
            throw;
        }
        view->ptrLock();
        *start = fresh;
        return job;
    }

    bool current(const Ref& job) const {
        if (!job || closing || job->cancelled->load()) return false;
        const auto found = jobs.find(job->url);
        return found != jobs.end() && found->second == job;
    }

    struct Completion {
        size_t attempted = 0, failures = 0;
        bool finished = true;
    };

    // Seal before the first view call. A new request for this URL gets a new
    // flight; finishing the old flight must not erase or unpin that replacement.
    // false defers this member without consuming its pin. Exceptions consume
    // this attempt, as complete() did, and cannot strand later members.
    template <class Apply>
    Completion completeSome(const Ref& job, size_t limit, Apply&& apply) {
        if (!job || closing || job->cancelled->load()) return {};
        if (!job->sealed && !current(job)) return {};
        Completion result;
        if (limit != 0) {
            if (!job->sealed) {
                jobs.erase(job->url);
                job->sealed = true;
            }
            while (!job->members.empty() && result.attempted < limit) {
                View* view = job->members.front();
                bool consumed = true;
                try { consumed = apply(view, job->payload); }
                catch (...) { ++result.failures; }
                if (!consumed) break;
                ++result.attempted;
                const auto found = active.find(view);
                if (found != active.end() && found->second.job == job) release(found);
            }
        }
        result.finished = job->members.empty();
        return result;
    }

    // Existing unbounded callers retain their per-view completion contract.
    template <class Apply>
    size_t complete(const Ref& job, Apply&& apply) {
        return completeSome(job, std::numeric_limits<size_t>::max(),
            [&](View* view, Payload& payload) { apply(view, payload); return true; }).failures;
    }

    void finish(const Ref& job) {
        if (!job) return;
        while (!job->members.empty()) {
            const auto found = active.find(job->members.front());
            release(found);
        }
    }

    void cancel(View* view) {
        const auto found = active.find(view);
        if (found != active.end()) release(found);
    }

    void close() {
        if (closing) return;
        closing = true;
        while (!active.empty()) release(active.begin());
    }

    bool isClosing() const { return closing; }

    struct Snapshot { size_t requests, keyBytes, consumers; };
    Snapshot snapshot() const noexcept {
        return {credits->requests.load(std::memory_order_acquire),
            credits->bytes.load(std::memory_order_acquire), active.size()};
    }

private:
    struct Consumer {
        Ref job;
        typename std::list<View*>::iterator node;
    };
    using Map = std::unordered_map<View*, Consumer>;
    Map active;
    std::unordered_map<std::string, Ref> jobs;
    const std::shared_ptr<Credits> credits = std::make_shared<Credits>();
    bool closing = false;

    void release(typename Map::iterator found) {
        const Ref job = found->second.job;
        View* view = found->first;
        job->members.erase(found->second.node);
        active.erase(found);
        if (job->members.empty()) {
            const auto lookup = jobs.find(job->url);
            if (lookup != jobs.end() && lookup->second == job) jobs.erase(lookup);
            job->cancelled->store(true);
        }
        view->ptrUnlock();
    }
};

} // namespace ps5::artwork
