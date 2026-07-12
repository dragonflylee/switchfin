/*
    GMCA — Now Playing screen for music (SPEC.md — issue #11).
    A pushed activity (like PlayerView) that observes the AudioPlayer controller:
    cover + title/artist + transport + shuffle/repeat. Audio keeps playing when
    it is popped (the controller owns playback).
*/

#pragma once

#include <borealis.hpp>
#include <api/media/types.hpp>
#include <utils/event.hpp>

class VideoProgressSlider;
class SVGImage;
class RecyclingGrid;

class MusicNowPlaying : public brls::Box {
public:
    MusicNowPlaying();
    ~MusicNowPlaying() override;

    brls::View* getDefaultFocus() override { return this->btnToggle; }

    /// Start playing `tracks[index]` and open the Now Playing screen.
    static void present(const std::vector<media::Item>& tracks, size_t index, bool shuffle = false);
    /// Open the Now Playing screen over the CURRENT queue (no restart) — used by
    /// the persistent mini-bar.
    static void open();

private:
    void refreshTrack();          // cover + title/artist from the controller
    void refreshShuffle();        // shuffle button border state
    void refreshRepeat();         // repeat icon
    void rebuildQueue();          // (re)load the queue pane from the controller
    void refreshQueueHighlight();  // move the now-playing marker without a reload

    BRLS_BIND(brls::Image, cover, "musicnp/cover");
    BRLS_BIND(brls::Label, labelTitle, "musicnp/title");
    BRLS_BIND(brls::Label, labelArtist, "musicnp/artist");
    BRLS_BIND(VideoProgressSlider, slider, "musicnp/progress");
    BRLS_BIND(brls::Label, leftStatus, "musicnp/left");
    BRLS_BIND(brls::Label, rightStatus, "musicnp/right");
    BRLS_BIND(brls::Box, btnShuffle, "musicnp/shuffle");
    BRLS_BIND(brls::Box, btnPrev, "musicnp/prev");
    BRLS_BIND(brls::Box, btnToggle, "musicnp/toggle");
    BRLS_BIND(brls::Box, btnNext, "musicnp/next");
    BRLS_BIND(brls::Box, btnRepeat, "musicnp/repeat");
    BRLS_BIND(SVGImage, toggleIcon, "musicnp/toggle/icon");
    BRLS_BIND(SVGImage, repeatIcon, "musicnp/repeat/icon");
    BRLS_BIND(RecyclingGrid, queueList, "musicnp/queue");

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription trackSubscribeID;
    brls::VoidEvent::Subscription queueSubscribeID;
};
