//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <client/client.hpp>
#include <utils/event.hpp>

class VideoProgressSlider;
class SVGImage;

class MusicView : public brls::Box, public brls::Singleton<MusicView> {
    enum RepeatMode { RepeatNone, RepeatOne, RepeatAll };

public:
    MusicView();
    ~MusicView() override;

    bool isTranslucent() override { return true; }

    void registerViewAction(brls::View* view);

    void load(const std::vector<remote::DirEntry>& items, size_t index, const std::string& extra);

private:
    BRLS_BIND(brls::Box, btnPrev, "music/prev");
    BRLS_BIND(brls::Box, btnNext, "music/next");
    BRLS_BIND(brls::Box, btnToggle, "music/toggle");
    BRLS_BIND(brls::Box, btnSuffle, "music/shuffle");
    BRLS_BIND(brls::Box, btnRepeat, "music/repeat");
    BRLS_BIND(SVGImage, btnRepeatIcon, "music/repeat/icon");
    BRLS_BIND(SVGImage, btnToggleIcon, "music/toggle/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "music/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "music/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "music/right/status");

    bool toggleShuffle();

    bool toggleLoop();

    void registerMpvEvent();

    void unregisterMpvEvent();

    void reset();

    MPVEvent::Subscription eventSubscribeID;

    int64_t playSession = 0;

    RepeatMode repeat = RepeatNone;
};