//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "api/jellyfin/media.hpp"

class VideoProgressSlider;
class SVGImage;

// https://github.com/mpv-player/mpv/blob/master/DOCS/edl-mpv.rst
class EDLUrl {
public:
    std::string url;
    float length = -1;  // second

    EDLUrl(std::string url, float length = -1) : url(url), length(length) {}
};

enum class OSDState {
    HIDDEN = 0,
    SHOWN = 1,
    ALWAYS_ON = 2,
};

class VideoView : public brls::Box {
public:
    VideoView(jellyfin::MediaItem& item);

    ~VideoView() override;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

    void invalidate() override;

    void onLayout() override;

    View* getDefaultFocus() override { return this->isOsdShown ? this->btnToggle : this; }

    void onChildFocusGained(View* directChild, View* focusedView) override;

    View* getNextFocus(brls::FocusDirection direction, View* currentView) override { return this; }

    void setTitie(const std::string& title);

private:
    /// OSD
    BRLS_BIND(brls::Label, titleLabel, "video/osd/title");
    BRLS_BIND(brls::ProgressSpinner, osdSpinner, "video/osd/loading");
    BRLS_BIND(VideoProgressSlider, osdSlider, "video/osd/bottom/progress");
    BRLS_BIND(brls::Box, btnSetting, "video/osd/setting");
    BRLS_BIND(brls::Box, btnToggle, "video/osd/toggle");
    BRLS_BIND(SVGImage, btnToggleIcon, "video/osd/toggle/icon");
    BRLS_BIND(brls::Box, osdTopBox, "video/osd/top/box");
    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");
    BRLS_BIND(brls::Box, osdCenterBox, "video/osd/center/box");
    BRLS_BIND(brls::Label, centerLabel, "video/osd/center/label");

    /// @brief get video url
    void doPlaybackInfo();
    void reportStart();
    void reportStop();
    void reportPlay(bool isPaused = false);

    void registerMpvEvent();
    void unRegisterMpvEvent();

    void showLoading();
    void hideLoading();
    /// OSD
    void toggleOSD();
    void showOSD(bool autoHide = true);
    void hideOSD();
    bool showSetting();

    // OSD
    bool isOsdShown = false;
    time_t osdLastShowTime = 0;
    const time_t OSD_SHOW_TIME = 5;  //默认5秒
    OSDState osd_state = OSDState::HIDDEN;

    MPVEvent::Subscription eventSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    brls::Rect oldRect = brls::Rect(-1, -1, -1, -1);

    std::string itemId;
    jellyfin::MediaSource itemSource;
    jellyfin::UserDataResult *userData;
};
