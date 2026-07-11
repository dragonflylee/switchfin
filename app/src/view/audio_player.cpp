#include "view/audio_player.hpp"
#include "view/mpv_core.hpp"
#include "api/backend.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include <algorithm>
#include <chrono>
#include <random>

using namespace brls::literals;  // for _i18n

/// marks a track as "played" past this fraction (kept identical to the video
/// scrobble threshold for now — SPEC.md §13)
static constexpr double SCROBBLE_THRESHOLD = 0.90;

/// a track carries a directly playable source
static bool hasAccessible(const media::Item& it) {
    for (auto& m : it.media)
        for (auto& p : m.parts)
            if (p.accessible && p.exists && !p.key.empty()) return true;
    return false;
}

/// server progress reports pollute the real library in capture mode; playback
/// itself stays enabled so the harness can hear/verify audio (SPEC.md).
static bool reportingDisabled() { return std::getenv("GMCA_NAV_PIPE") != nullptr; }

// AudioPlayer is a process-lifetime singleton, so async callbacks capture `this`
// directly (no brls::View deletion-token dance).

AudioPlayer::AudioPlayer() { brls::Logger::debug("AudioPlayer: create"); }

AudioPlayer::~AudioPlayer() { this->unsubscribe(); }

const media::Item& AudioPlayer::current() const {
    static const media::Item empty{};
    if (this->pos < 0 || this->pos >= (int)this->order.size()) return empty;
    return this->queue[this->order[this->pos]];
}

void AudioPlayer::play(const std::vector<media::Item>& tracks, size_t index, bool shuffle) {
    if (tracks.empty()) return;
    this->queue = tracks;
    this->isShuffle = shuffle;
    // reset before rebuilding so rebuildOrder can never read a stale `order`
    // permutation left over from a previous (differently sized) queue
    this->order.clear();
    this->pos = -1;
    this->rebuildOrder((int)std::min(index, tracks.size() - 1));
    this->owns = true;
    this->subscribe();
    this->playCurrent();
}

void AudioPlayer::rebuildOrder(int targetQueueIdx) {
    if (targetQueueIdx < 0 || targetQueueIdx >= (int)this->queue.size()) targetQueueIdx = 0;
    // identity order over the CURRENT queue (never reads the old `order`)
    this->order.resize(this->queue.size());
    for (size_t i = 0; i < this->order.size(); i++) this->order[i] = i;
    if (this->isShuffle) {
        auto seed = (unsigned)std::chrono::steady_clock::now().time_since_epoch().count();
        std::shuffle(this->order.begin(), this->order.end(), std::mt19937(seed));
        // bring the target track to the front so it plays first
        auto it = std::find(this->order.begin(), this->order.end(), (size_t)targetQueueIdx);
        if (it != this->order.end()) std::iter_swap(this->order.begin(), it);
        this->pos = 0;
    } else {
        this->pos = targetQueueIdx;  // identity order: position == queue index
    }
}

void AudioPlayer::playCurrent() {
    if (this->pos < 0 || this->pos >= (int)this->order.size()) return;
    this->scrobbled = false;
    this->sessionId = misc::randHex(12);
    const media::Item& track = this->current();
    MPVCore::instance().enableVO(false);
    this->trackChanged.fire();
    if (hasAccessible(track)) {
        this->resolveAndPlay(track);
    } else {
        // list items (getChildren) usually lack Media/Part — fetch full detail
        std::string id = track.ratingKey;
        AppConfig::instance().backend().getItemDetail(
            id, true,
            [this, id](const media::Item& detailed) {
                // guard against a skip while the request was in flight
                if (this->current().ratingKey != id) return;
                this->queue[this->order[this->pos]] = detailed;
                this->trackChanged.fire();
                this->resolveAndPlay(detailed);
            },
            [](const std::string& ex) { brls::Application::notify(ex); });
    }
}

void AudioPlayer::resolveAndPlay(const media::Item& track) {
    const media::Media* chosen = nullptr;
    for (auto& m : track.media) {
        for (auto& p : m.parts)
            if (p.accessible && p.exists && !p.key.empty()) {
                chosen = &m;
                break;
            }
        if (chosen) break;
    }
    if (!chosen) {
        brls::Application::notify("main/player/error"_i18n);
        return;
    }
    media::PlaybackOptions opts;
    opts.seekMs = track.viewOffset;
    opts.bitrateCap = MPVCore::VIDEO_QUALITY;
    opts.forceDirectPlay = MPVCore::FORCE_DIRECTPLAY;
    opts.sessionId = this->sessionId;
    std::string wantId = track.ratingKey;
    media::Item item = track;
    media::Media version = *chosen;
    brls::async([this, wantId, item, version, opts]() {
        try {
            media::PlaybackSource src = AppConfig::instance().backend().resolvePlayback(item, version, opts);
            brls::sync([this, wantId, src]() {
                // a skip may have superseded this resolve
                if (!this->owns || this->current().ratingKey != wantId) return;
                if (src.url.empty()) {
                    brls::Application::notify("main/player/error"_i18n);
                    return;
                }
                MPVCore::instance().setUrl(src.url, src.mpvExtra);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([msg]() { brls::Application::notify(msg); });
        }
    });
}

void AudioPlayer::toggle() {
    auto& mpv = MPVCore::instance();
    if (mpv.isStopped())
        this->playCurrent();
    else
        mpv.togglePlay();
}

void AudioPlayer::next() {
    if (this->order.empty()) return;
    this->pos = (this->pos + 1) % (int)this->order.size();  // wrap on manual skip
    this->playCurrent();
}

void AudioPlayer::prev() {
    if (this->order.empty()) return;
    this->pos = (this->pos - 1 + (int)this->order.size()) % (int)this->order.size();
    this->playCurrent();
}

void AudioPlayer::seekPercent(float percent) {
    MPVCore::instance().seek((int64_t)(percent * 100), "absolute-percent");
}

void AudioPlayer::setShuffle(bool on) {
    if (this->isShuffle == on) return;
    this->isShuffle = on;
    // keep the currently playing track: read its queue index from the CURRENT
    // (still valid) order before rebuilding
    int curQueueIdx = (this->pos >= 0 && this->pos < (int)this->order.size()) ? (int)this->order[this->pos] : 0;
    this->rebuildOrder(curQueueIdx);
}

AudioPlayer::Repeat AudioPlayer::cycleRepeat() {
    this->repeat = this->repeat == Repeat::None  ? Repeat::One
                   : this->repeat == Repeat::One ? Repeat::All
                                                 : Repeat::None;
    return this->repeat;
}

void AudioPlayer::advanceAuto() {
    if (this->repeat == Repeat::One) {
        this->playCurrent();
        return;
    }
    if (this->pos + 1 < (int)this->order.size()) {
        this->pos++;
        this->playCurrent();
    } else if (this->repeat == Repeat::All) {
        this->pos = 0;
        this->playCurrent();
    }
    // else: end of queue, stop (keep queue for a manual replay)
}

void AudioPlayer::reportTimeline(const std::string& state, int64_t timeMs) {
    if (reportingDisabled()) return;
    const media::Item& track = this->current();
    if (track.ratingKey.empty()) return;
    media::PlayState st = state == "paused"    ? media::PlayState::Paused
                          : state == "stopped" ? media::PlayState::Stopped
                                               : media::PlayState::Playing;
    AppConfig::instance().backend().reportProgress(track.ratingKey, st, timeMs, track.duration, this->sessionId);
}

void AudioPlayer::maybeScrobble(int64_t timeMs) {
    if (reportingDisabled() || this->scrobbled) return;
    const media::Item& track = this->current();
    if (track.duration <= 0) return;
    if (double(timeMs) / double(track.duration) < SCROBBLE_THRESHOLD) return;
    this->scrobbled = true;
    AppConfig::instance().backend().markWatched(track.ratingKey);
}

void AudioPlayer::subscribe() {
    if (this->subscribed) return;
    this->subscribed = true;
    this->eventSubscribeID = MPVCore::instance().getEvent()->subscribe([this](MpvEventEnum event) {
        if (!this->owns) return;
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->reportTimeline("paused", int64_t(mpv.video_progress) * 1000);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportTimeline("playing", int64_t(mpv.playback_time) * 1000);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            if (mpv.video_progress % 10 == 0) {
                this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
                this->maybeScrobble(int64_t(mpv.video_progress) * 1000);
            }
            break;
        case MpvEventEnum::END_OF_FILE:
            // track finished on its own: count as played, then honor repeat
            this->maybeScrobble(this->current().duration);
            this->advanceAuto();
            break;
        default:;
        }
    });
}

void AudioPlayer::unsubscribe() {
    if (!this->subscribed) return;
    MPVCore::instance().getEvent()->unsubscribe(this->eventSubscribeID);
    this->subscribed = false;
}

void AudioPlayer::release() {
    if (!this->owns) return;
    this->owns = false;
    this->unsubscribe();
    this->queue.clear();
    this->order.clear();
    this->pos = -1;
    this->trackChanged.fire();  // observers (mini-bar) refresh -> hide when inactive
}
