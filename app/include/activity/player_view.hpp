/*
    GMCA — Plex video player.
    Pipeline: PLEX_MIGRATION.md §2.7 (direct play, universal transcoder, timeline, scrobble).
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/plex/types.hpp>

class VideoView;

class PlayerView : public brls::Box {
public:
    /// versionIndex selects which item.media[] source to play (default -1 = the
    /// first accessible version, i.e. unchanged Plex/Jellyfin behavior). The
    /// Stremio source picker passes an explicit index to honor the user's choice.
    PlayerView(const plex::Item& item, const int64_t seekMs = 0, int versionIndex = -1);
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
    /// Fetches fresh metadata then resolves the playback URL via the backend
    void playMedia(const int64_t seekMs);
    /// Resolves the playback URL (direct/transcode) through the active backend
    void startPlayback(const int64_t seekMs);
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
    /// stable play-session id for the whole playback session
    std::string sessionId;
    plex::Item item;     // fresh metadata (media/chapters/markers)
    plex::Media stream;  // selected version
    /// caller-chosen source index (Stremio picker); -1 = first accessible.
    /// Reset to -1 on episode switch so binge auto-picks the best source.
    int preferredVersion = -1;
    bool scrobbled = false;
    std::vector<plex::Item> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};
