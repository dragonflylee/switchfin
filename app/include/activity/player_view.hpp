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
    PlayerView(const jellyfin::Item& item);
    ~PlayerView();

    void setSeries(const std::string& seriesId);
    void setTitie(const std::string& title);

private:
    void setChapters(const std::vector<jellyfin::MediaChapter>& chaps, uint64_t duration);
    /// @brief get video url
    void playMedia(const uint64_t seekTicks);
    bool playIndex(int index);
    void reportStart();
    void reportStop();
    void reportPlay(bool isPaused = false);
    void requestDanmaku();

    // Playinfo
    std::string itemId;
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