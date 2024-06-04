//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/event.hpp>

class VideoProgressSlider;
class SVGImage;

class MusicView : public brls::Box, public brls::Singleton<MusicView> {
public:
    MusicView();
    ~MusicView() override;

    bool isTranslucent() override { return true; }

    void registerViewAction(brls::View* view);

    const std::string& currentId();

    void play(const jellyfin::MediaItem& item);

    void load(const std::vector<jellyfin::MusicTrack>& item, size_t index);

private:
    BRLS_BIND(brls::Box, btnPrev, "music/prev");
    BRLS_BIND(brls::Box, btnNext, "music/next");
    BRLS_BIND(brls::Box, btnToggle, "music/toggle");
    BRLS_BIND(brls::Box, btnSuffle, "music/shuffle");
    BRLS_BIND(brls::Box, btnRepeat, "music/repeat");
    BRLS_BIND(SVGImage, btnToggleIcon, "music/toggle/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "music/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "music/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "music/right/status");
    BRLS_BIND(brls::Label, playTitle, "music/play/title");

    bool toggleShuffle();

    bool toggleLoop();

    void registerMpvEvent();

    void unregisterMpvEvent();

    void reset();

    MPVEvent::Subscription eventSubscribeID;
    MPVCommandReply::Subscription replySubscribeID;

    using MusicList = std::unordered_map<int64_t, jellyfin::MusicTrack>;
    int64_t playSession = 0;
    std::string itemId;
    MusicList playList;
};