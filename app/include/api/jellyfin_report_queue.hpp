#pragma once

#include "api/jellyfin.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <list>

namespace jellyfin {

// UI-thread owned, including timer and HTTP completion callbacks. Limits cover
// retained logical string bytes, not allocator overhead or HTTP/dispatch copies.
class ReportQueue : public std::enable_shared_from_this<ReportQueue> {
public:
    using Clock = std::chrono::steady_clock;
    // Called on UI and worker threads; injected clocks must support both.
    using Now = std::function<Clock::time_point()>;
    static constexpr size_t MAX_SESSIONS = 8;
    static constexpr size_t MAX_REPORT_BYTES = 8 * 1024;
    static constexpr size_t MAX_IDENTITY_BYTES = 8 * 1024;
    static constexpr size_t MAX_HEADERS = 16;
    static constexpr size_t MAX_RETAINED_BYTES = 128 * 1024;
    static constexpr auto MAX_UNSENT_AGE = std::chrono::seconds(120);

    struct Stats {
        size_t sessions = 0, reports = 0, bytes = 0;
        size_t highSessions = 0, highReports = 0, highBytes = 0;
        uint64_t dropped = 0, expired = 0, evictedSessions = 0;
        uint64_t submissionFailures = 0, responseFailures = 0, completionDispatchFailures = 0, timerFailures = 0;
    };

    explicit ReportQueue(Now now = [] { return Clock::now(); }) : now(std::move(now)) {}

    Stats stats() const {
        auto result = counters;
        result.sessions = sessions.size();
        result.reports = pending.size() + (busy ? 1 : 0);
        result.bytes = retainedBytes();
        return result;
    }

    void post(RequestContext context, std::string endpoint, nlohmann::json data) {
        uint64_t addedSession = 0;
        try {
            pollCompletion();
            expire();
            const Kind kind = endpoint == apiPlayStart ? Kind::Start
                : endpoint == apiPlaying ? Kind::Progress : endpoint == apiPlayStop ? Kind::Stop : Kind::Invalid;
            if (kind == Kind::Invalid || !data.is_object() || context.headers.size() > MAX_HEADERS) {
                ++counters.dropped;
                return;
            }
            auto item = data.value("ItemId", std::string());
            auto playback = data.value("PlaySessionId", std::string());
            size_t identityBytes = context.server.size() + context.user.size() + item.size() + playback.size();
            for (const auto& header : context.headers) identityBytes += header.size();
            if (item.empty() || identityBytes > MAX_IDENTITY_BYTES) { ++counters.dropped; return; }
            auto body = data.dump();
            if (body.size() > MAX_REPORT_BYTES) { ++counters.dropped; return; }
            auto match = std::find_if(sessions.rbegin(), sessions.rend(), [&](const Session& s) {
                return s.item == item && s.playback == playback && s.context.server == context.server &&
                    s.context.user == context.user && s.context.headers == context.headers &&
                    (kind == Kind::Stop || (s.context.cancel == context.cancel &&
                        s.context.ownerCancel == context.ownerCancel));
            });
            auto session = match == sessions.rend() ? sessions.end() : std::prev(match.base());
            if (kind == Kind::Start) {
                if ((session != sessions.end() && !session->stopping && !session->retired) ||
                    !makeRoom(identityBytes + body.size(), true, 0)) {
                    ++counters.dropped;
                    return;
                }
                addedSession = ++nextSession;
                // Copy the admitted context into fresh containers: moving a
                // caller's mostly empty but over-reserved strings/vector would
                // retain storage unrelated to the admitted string lengths.
                sessions.push_back({addedSession, context, std::move(item), std::move(playback), identityBytes});
                session = std::prev(sessions.end());
            } else if (session == sessions.end() || session->retired || session->stopping) {
                // Evicted/expired Starts have no unbounded tombstone map. Later
                // orphan samples cannot reintroduce that session or its secrets.
                ++counters.dropped;
                return;
            }
            const auto id = session->id;
            // Only the latest unsent progress survives for each session. Removing
            // the older sample retains the order of every remaining barrier.
            if (kind == Kind::Progress) {
                auto old = std::find_if(pending.begin(), pending.end(), [id](const Post& p) {
                    return p.session == id && p.kind == Kind::Progress;
                });
                if (old != pending.end()) pending.erase(old);
            }
            if (!makeRoom(body.size(), false, id)) { ++counters.dropped; return; }
            pending.push_back({id, kind, std::move(body), now()});
            if (kind == Kind::Stop) session->stopping = true;
            recordHighWater();
        } catch (...) {
            ++counters.dropped;
            if (addedSession) removeSession(addedSession, false);
        }
        pump();
    }

private:
    enum class Kind { Invalid, Start, Progress, Stop };
    struct Session {
        uint64_t id;
        RequestContext context;
        std::string item, playback;
        size_t bytes;
        bool stopping = false, retired = false;
    };
    struct Post { uint64_t session; Kind kind; std::string body; Clock::time_point queued; };
    struct Completion {
        // Worker publishes only after transport and UI submission have finished.
        enum : unsigned { Finished = 1, Failed = 2, DispatchFailed = 4, Expired = 8 };
        std::atomic<unsigned> terminal{0};
        HTTP::Cancel cancel, ownerCancel;
        bool cancelled() const { return (cancel && cancel->load()) || (ownerCancel && ownerCancel->load()); }
    };
    std::shared_ptr<Completion> completion;
    std::list<Session> sessions;
    std::deque<Post> pending;
    Now now;
    Stats counters;
    uint64_t nextSession = 0, nextAttempt = 0, activeAttempt = 0, activeSession = 0;
    size_t activeBytes = 0;
    Kind activeKind = Kind::Invalid;
    bool busy = false, pumping = false, timerArmed = false;

    size_t retainedBytes() const {
        size_t result = activeBytes;
        for (const auto& session : sessions) result += session.bytes;
        for (const auto& post : pending) result += post.body.size();
        return result;
    }
    void recordHighWater() {
        const auto current = stats();
        counters.highSessions = std::max(counters.highSessions, current.sessions);
        counters.highReports = std::max(counters.highReports, current.reports);
        counters.highBytes = std::max(counters.highBytes, current.bytes);
    }
    void removeSession(uint64_t id, bool expired) {
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->session != id) { ++it; continue; }
            ++counters.dropped;
            if (expired) ++counters.expired;
            it = pending.erase(it);
        }
        auto session = std::find_if(sessions.begin(), sessions.end(), [id](const Session& s) { return s.id == id; });
        if (session == sessions.end()) return;
        // Transport ownership lasts until its real completion, even when all
        // unsent work expires. Never start a parallel attempt on an age timeout.
        if (busy && activeSession == id) session->retired = true;
        else sessions.erase(session);
    }
    bool makeRoom(size_t bytes, bool newSession, uint64_t keep) {
        while ((newSession && sessions.size() >= MAX_SESSIONS) || retainedBytes() + bytes > MAX_RETAINED_BYTES) {
            auto victim = std::find_if(sessions.begin(), sessions.end(), [&](const Session& s) {
                return s.id != keep && (!busy || s.id != activeSession);
            });
            if (victim == sessions.end()) return false;
            ++counters.evictedSessions;
            removeSession(victim->id, false);
        }
        return true;
    }
    void expire(bool all = false) {
        const auto time = now();
        for (;;) {
            auto old = std::find_if(pending.begin(), pending.end(), [&](const Post& p) {
                return all || time - p.queued >= MAX_UNSENT_AGE;
            });
            if (old == pending.end()) break;
            if (old->kind == Kind::Progress) {
                pending.erase(old);
                ++counters.dropped;
                ++counters.expired;
            } else removeSession(old->session, true);
        }
    }
    void armExpiry() {
        if (timerArmed || (!busy && pending.empty())) return;
        try {
            std::weak_ptr<ReportQueue> weak = shared_from_this();
            brls::delay(1000, [weak] {
                if (auto self = weak.lock()) {
                    self->timerArmed = false;
                    self->pollCompletion();
                    self->expire();
                    self->pump();
                }
            });
            timerArmed = true;
        } catch (...) {
            ++counters.timerFailures;
            // If the timer cannot be scheduled, release unsent work rather than
            // silently retaining it past an unserviceable expiry deadline.
            expire(true);
        }
    }
    void pollCompletion() {
        if (!busy || !completion) return;
        const auto terminal = completion->terminal.load(std::memory_order_acquire);
        if (!terminal) return;
        if (terminal & Completion::DispatchFailed) ++counters.completionDispatchFailures;
        complete(activeAttempt, (terminal & Completion::Failed) || completion->cancelled(),
            terminal & Completion::Expired);
    }
    void complete(uint64_t attempt, bool failed, bool expired = false) {
        if (!busy || activeAttempt != attempt) return;
        if (expired) {
            ++counters.expired;
            ++counters.dropped;
            // No HTTP Start was attempted. Even newer queued samples cannot
            // survive as orphan Progress/Stop for this reporting lifecycle.
            if (activeKind == Kind::Start) removeSession(activeSession, true);
        } else if (failed) ++counters.responseFailures;
        const auto sessionId = activeSession;
        const auto kind = activeKind;
        busy = false;
        completion.reset();
        activeBytes = 0;
        activeSession = 0;
        auto session = std::find_if(sessions.begin(), sessions.end(), [sessionId](const Session& s) { return s.id == sessionId; });
        if (session != sessions.end() && (kind == Kind::Stop || session->retired)) removeSession(sessionId, false);
        pump();
    }
    void pump() {
        if (pumping) return;
        pumping = true;
        // Completion may be synchronous in a service adapter. Drain iteratively,
        // and bind every completion to one attempt so duplicates cannot advance it.
        while (!busy) {
            expire();
            if (pending.empty()) break;
            auto next = std::move(pending.front());
            pending.pop_front();
            busy = true;
            activeSession = next.session;
            activeKind = next.kind;
            activeBytes = next.body.size();
            activeAttempt = ++nextAttempt;
            const auto attempt = activeAttempt;
            try {
                auto self = shared_from_this();
                auto session = std::find_if(sessions.begin(), sessions.end(), [&](const Session& s) { return s.id == next.session; });
                auto context = session->context;
                if (next.kind == Kind::Stop) { context.cancel.reset(); context.ownerCancel.reset(); }
                const std::string endpoint(next.kind == Kind::Start ? apiPlayStart
                    : next.kind == Kind::Progress ? apiPlaying : apiPlayStop);
                auto result = std::make_shared<Completion>();
                result->cancel = context.cancel;
                result->ownerCancel = context.ownerCancel;
                completion = result;
                brls::async([self, attempt, result, context = std::move(context),
                    endpoint, body = std::move(next.body), queued = next.queued, clock = now] {
                    bool failed = false, expired = false;
                    try {
                        auto headers = context.headers;
                        headers.push_back("Content-Type: application/json");
                        const auto url = context.server + endpoint;
                        // Accepted async work can wait behind unrelated tasks.
                        // Carry enqueue age into this worker and check at the
                        // HTTP boundary, without reading UI-owned queue fields.
                        expired = clock() - queued >= MAX_UNSENT_AGE;
                        if (!expired) {
                            if (context.cancelled()) throw std::runtime_error("Request cancelled.");
                            auto response = HTTP::post(url, body, headers,
                                HTTP::Timeout{}, context.transportCancellation());
                            // Preserve report endpoint response validation, including 204.
                            if (!response.empty()) {
                                const auto parsed = nlohmann::json::parse(response);
                                (void)parsed;
                            }
                        }
                    } catch (...) { failed = true; }
                    unsigned terminal = Completion::Finished | (failed ? Completion::Failed : 0) |
                        (expired ? Completion::Expired : 0);
                    try {
                        brls::sync([self, attempt, result, failed, expired] {
                            self->complete(attempt, failed || result->cancelled(), expired);
                        });
                    } catch (...) { terminal |= Completion::DispatchFailed; }
                    result->terminal.store(terminal, std::memory_order_release);
                });
            } catch (...) {
                // This call constructs captures, then brls::async does only a
                // locked vector::push_back. A throw means no worker was accepted.
                // Never retry an attempt; a submitted HTTP failure is ambiguous.
                ++counters.submissionFailures;
                if (busy && activeAttempt == attempt) {
                    if (activeKind == Kind::Start) removeSession(activeSession, false);
                    complete(attempt, false);
                }
            }
        }
        pumping = false;
        armExpiry();
    }
};

inline void postPlaybackReport(RequestContext context, std::string_view endpoint, nlohmann::json data) {
    static auto queue = std::make_shared<ReportQueue>();
    queue->post(std::move(context), std::string(endpoint), std::move(data));
}

} // namespace jellyfin
