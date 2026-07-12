/*
    GMCA — shared audio playback controller (SPEC.md — issue #11).

    Owns the music queue and drives MPVCore per track (resolvePlayback +
    server progress/scrobble), mirroring PlayerView's playback engine without
    its video UI. Observed by the mini-bar (MusicView) and the Now Playing
    screen. Local files and server tracks both funnel through play().
*/

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <api/media/types.hpp>
#include <utils/event.hpp>

class AudioPlayer : public brls::Singleton<AudioPlayer> {
public:
    enum class Repeat { None, One, All };

    AudioPlayer();
    ~AudioPlayer();

    /// Replace the queue and start playing `tracks[index]`. `shuffle` seeds the
    /// shuffle state (e.g. "play artist"). Reused by local + server callers.
    void play(const std::vector<media::Item>& tracks, size_t index, bool shuffle = false);

    void toggle();
    void next();
    void prev();
    void seekPercent(float percent);
    /// Seek one step (scaled to track length) forward or backward, clamped
    /// inside the track. Drives the Now Playing D-pad seek.
    void seekStep(bool forward);

    void setShuffle(bool on);
    bool shuffled() const { return this->isShuffle; }
    Repeat cycleRepeat();
    Repeat repeatMode() const { return this->repeat; }

    /// True while a queue is loaded and this controller owns MPVCore.
    bool active() const { return this->owns && !this->queue.empty(); }
    /// Current track (empty Item when inactive).
    const media::Item& current() const;

    // --- Queue inspection / edition (Now Playing queue pane) ------------------
    /// Number of tracks in the queue (playback order).
    size_t queueCount() const { return this->order.size(); }
    /// Index of the currently playing track within the playback order (-1 none).
    int queuePos() const { return this->pos; }
    /// Track at a playback-order position (empty Item when out of range).
    const media::Item& queueItemAt(size_t orderIndex) const;
    /// Jump to a playback-order position and start it.
    void playAt(size_t orderIndex);
    /// Move a queue entry from one playback-order position to another; the
    /// currently playing track stays current and playback is not interrupted.
    void moveInQueue(size_t from, size_t to);
    /// Fires when the queue contents/order change (load, shuffle, move) — the
    /// queue pane rebuilds. Track-only changes go through trackEvent() instead.
    brls::VoidEvent* queueEvent() { return &this->queueChanged; }

    /// Hand MPVCore back to another player (e.g. a video PlayerView starting):
    /// stop owning, unsubscribe, keep no queue. Called before video playback.
    void release();

    /// Fires on queue load and on every track change (UI re-reads current()).
    brls::VoidEvent* trackEvent() { return &this->trackChanged; }

private:
    void playCurrent();
    void resolveAndPlay(const media::Item& track);
    void advanceAuto();  // natural end-of-file: honor repeat mode
    void reportTimeline(const std::string& state, int64_t timeMs);
    void maybeScrobble(int64_t timeMs);
    /// (Re)build `order` over the CURRENT queue and set `pos` so that
    /// `targetQueueIdx` (an index into `queue`) becomes the current track.
    /// Never reads the pre-existing `order`, so it is safe across queue swaps.
    void rebuildOrder(int targetQueueIdx);
    void subscribe();
    void unsubscribe();

    std::vector<media::Item> queue;  // tracks in their natural order
    std::vector<size_t> order;       // playback order (identity, or shuffled)
    int pos = -1;                    // position within `order`
    bool isShuffle = false;
    Repeat repeat = Repeat::None;
    bool owns = false;   // this controller currently drives MPVCore
    bool scrobbled = false;
    std::string sessionId;

    MPVEvent::Subscription eventSubscribeID;
    bool subscribed = false;
    brls::VoidEvent trackChanged;
    brls::VoidEvent queueChanged;
};
