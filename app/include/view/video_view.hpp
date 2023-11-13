//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>
#include "view/mpv_core.hpp"
#include "api/jellyfin/media.hpp"

class VideoProgressSlider;
class SVGImage;
class VideoProfile;

enum class OSDState {
    HIDDEN = 0,
    SHOWN = 1,
    ALWAYS_ON = 2,
};

enum class ClickState {
    IDLE = 0,
    PRESS = 1,
    FAST_RELEASE = 3,
    FAST_PRESS = 4,
    CLICK_DOUBLE = 5,
};

class VideoView : public brls::Box {
public:
    VideoView(const std::string& itemId);

    ~VideoView() override;

    void setSeries(const std::string& seriesId);

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
    BRLS_BIND(brls::Box, btnForward, "video/osd/forward");
    BRLS_BIND(brls::Box, btnBackward, "video/osd/backward");
    BRLS_BIND(brls::Box, btnSetting, "video/osd/setting");
    BRLS_BIND(brls::Box, btnCast, "video/osd/cast");
    BRLS_BIND(brls::Box, btnToggle, "video/osd/toggle");
    BRLS_BIND(brls::Box, btnVideoQuality, "video/quality/box");
    BRLS_BIND(brls::Box, btnVideoChapter, "video/chapter/box");
    BRLS_BIND(brls::Box, btnVideoSpeed, "video/speed/box");
    BRLS_BIND(SVGImage, btnToggleIcon, "video/osd/toggle/icon");
    BRLS_BIND(brls::Box, osdTopBox, "video/osd/top/box");
    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");
    BRLS_BIND(brls::Box, osdCenterBox, "video/osd/center/box");
    BRLS_BIND(brls::Label, centerLabel, "video/osd/center/label");
    BRLS_BIND(brls::Label, leftStatusLabel, "video/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "video/right/status");
    BRLS_BIND(brls::Label, videoChapterLabel, "video/chapter");
    BRLS_BIND(brls::Label, videoQualityLabel, "video/quality");
    BRLS_BIND(brls::Label, videoSpeedLabel, "video/speed");
    BRLS_BIND(brls::Label, speedHintLabel, "video/speed/hint/label");
    BRLS_BIND(brls::Box, speedHintBox, "video/speed/hint/box");
    BRLS_BIND(brls::Label, hintLabel, "video/osd/hint/label");
    BRLS_BIND(brls::Box, hintBox, "video/osd/hint/box");

    /// @brief get video url
    void playMedia(const time_t seekTicks);
    bool playNext(int off = 1);
    void reportStart();
    void reportStop();
    void reportPlay(bool isPaused = false);

    void registerMpvEvent();
    void unRegisterMpvEvent();

    void showLoading();
    void hideLoading();
    void togglePlay();
    bool toggleProfile();
    /// OSD
    void toggleOSD();
    void showOSD(bool autoHide = true);
    void hideOSD();
    bool toggleSpeed();
    void showSetting();
    void showHint(const std::string& value);

    /// @brief 延迟 200ms 触发进度跳转到 seeking_range
    void requestSeeking();
    void buttonProcessing();
    /// @brief notify videoview closed
    static void onDismiss();

    // OSD
    bool isOsdShown = false;
    time_t osdLastShowTime = 0;
    time_t hintLastShowTime = 0;
    time_t profileLastShowTime = 0;
    const time_t OSD_SHOW_TIME = 5000000;  //默认5秒
    OSDState osdState = OSDState::HIDDEN;
    VideoProfile* profile;

    int64_t seekingRange = 0;
    size_t seekingIter = 0;

    MPVEvent::Subscription eventSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Rect oldRect = brls::Rect(-1, -1, -1, -1);
    brls::InputManager* input;

    // Touch Event
    size_t speedIter = 0;
    bool ignoreSpeed = false;
    ClickState clickState = ClickState::IDLE;
    brls::Time pressTime;
    size_t tapIter = 0;

    // Playinfo
    std::string itemId;
    std::vector<jellyfin::MediaChapter> chapters;
    /// @brief DirectPlay, Transcode
    std::string playMethod;
    std::string playSessionId;
    inline static int selectedQuality = 0;
    size_t itemIndex = -1;
    jellyfin::MediaSource itemSource;
    std::vector<jellyfin::MediaEpisode> showEpisodes;
};
