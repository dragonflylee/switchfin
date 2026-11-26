/*
    Copyright 2024 dragonflylee
*/

#pragma once

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
        brls::Application::getTheme().addColor("brls/clear", savedClearColor);
    }

    void willAppear(bool resetState) override {
        savedClearColor = brls::Application::getTheme().getColor("brls/clear");
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

private:
#ifdef ANDROID
    NVGcolor savedClearColor = nvgRGBA(0, 0, 0, 0);
#endif
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
    jellyfin::Source stream;
    std::vector<jellyfin::Episode> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};