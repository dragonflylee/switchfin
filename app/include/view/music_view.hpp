//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include "view/mpv_core.hpp"

class VideoProgressSlider;
class SVGImage;

class MusicView : public brls::Box {
public:
    MusicView();
    ~MusicView() override;

    bool isTranslucent() override { return true; }

    void load(const std::vector<jellyfin::MusicTrack>& list);

    static View* create();

private:
    BRLS_BIND(brls::Box, btnPrev, "music/prev");
    BRLS_BIND(brls::Box, btnNext, "music/next");
    BRLS_BIND(brls::Box, btnToggle, "music/toggle");
    BRLS_BIND(SVGImage, btnToggleIcon, "music/toggle/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "music/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "music/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "music/right/status");

    MPVEvent::Subscription eventSubscribeID;
};