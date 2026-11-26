/*
    Copyright 2024 dragonflylee
*/

#pragma once

#include <map>

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/jellyfin/media.hpp>

class VideoView;

class PlayerView : public brls::Box {
public:
    PlayerView(const jellyfin::Item& item, const uint64_t seekTicks = 0, const std::string& = "");
    ~PlayerView();

    void setSeries(const std::string& seriesId);
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
    void setChapters(const std::vector<jellyfin::MediaChapter>& chaps, uint64_t duration);
    /// @brief get video url
    void playMedia(const uint64_t seekTicks);
    bool playIndex(int index);
    void reportStart();
    void reportStop();
    void reportPlay(bool isPaused = false);
    void requestDanmaku();
    bool toggleQuality();

    // Playinfo
    std::string itemId;
    std::string sourceId;
    /// @brief DirectPlay, Transcode
    std::string playMethod;
    std::string playSessionId;
    /// Jellyfin stream indexes in use, reported so the server can remember them
    int reportAudio = -1;
    int reportSubtitle = -1;
    /// mpv track id -> Jellyfin stream index, to report picks made in mpv's own menus
    std::map<int64_t, int> audioTrackIndex, subTrackIndex;
    int lastAudio = -1, lastSubtitle = -1;
    void mapTracks();
    void applyDefaultTracks();
    int currentAudioIndex();
    int currentSubtitleIndex();
    jellyfin::Source stream;
    std::vector<jellyfin::Episode> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};