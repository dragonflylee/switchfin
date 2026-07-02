/*
    pleNx — Plex video player.
    Pipeline: PLEX_MIGRATION.md §2.7 (direct play, universal transcoder, timeline, scrobble).
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/plex/types.hpp>

class VideoView;

class PlayerView : public brls::Box {
public:
    PlayerView(const plex::Item& item, const int64_t seekMs = 0);
    ~PlayerView();

    /// Loads the show's episode list (previous/next navigation)
    void setSeries(const std::string& showRatingKey);
    void setTitie(const std::string& title);

#ifdef ANDROID
    void willDisappear(bool resetState) override {
        if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(235, 235, 235, 255));
        else
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(45, 45, 45, 255));
    }

    void willAppear(bool resetState) override {
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

private:
    void setChapters(const std::vector<plex::Chapter>& chaps, int64_t durationMs);
    /// Fetches fresh metadata then picks direct play or transcode
    void playMedia(const int64_t seekMs);
    void playDirect(const int64_t seekMs);
    void playTranscode(const int64_t seekMs);
    /// Tears the current transcode session down server-side (no-op for direct
    /// play). Fire-and-forget; safe to call after `this` is gone.
    void stopTranscode();
    /// On a transcode playback error, retry once in direct play (helps Vita,
    /// where the hardware decoder can choke on the transcoded stream). Returns
    /// true when a fallback was started (so the error dialog is suppressed).
    bool tryDirectPlayFallback();
    bool playIndex(int index);
    /// POST /:/timeline report (time/duration in ms)
    void reportTimeline(const std::string& state, int64_t timeMs);
    void reportStop();
    /// Marks as watched via /:/scrobble beyond the threshold (90%)
    void maybeScrobble(int64_t timeMs);
    bool toggleQuality();

    // Playback
    std::string itemId;  // ratingKey
    /// playMethod: "directplay" | "transcode" (VideoProfile display)
    std::string playMethod;
    /// X-Plex-Session-Identifier: stable for the whole playback session
    std::string sessionId;
    /// transcoder session: regenerated on every (re)start
    std::string transcodeSession;
    plex::Item item;     // fresh metadata (media/chapters/markers)
    plex::Media stream;  // selected version
    bool scrobbled = false;
    /// guards tryDirectPlayFallback so a failing stream falls back at most once
    /// per (re)load; reset by playMedia on every deliberate (re)start
    bool directPlayFallback = false;
    std::vector<plex::Item> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};
