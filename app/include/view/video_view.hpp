//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>

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
    VideoView();

    ~VideoView() override;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

    void invalidate() override;

    void onLayout() override;

    View* getDefaultFocus() override { return this->isOsdShown ? this->btnToggle : this; }

    void onChildFocusGained(View* directChild, View* focusedView) override;

    View* getNextFocus(brls::FocusDirection direction, View* currentView) override { return this; }

    void setTitie(const std::string& title);

    void setList(const std::vector<std::string>& values, int index = -1);

    void setDanmakuEnable(brls::Visibility v);

    void setClipPoint(const std::vector<float>& clips);

    brls::Event<int>* getPlayEvent() { return &this->playIndexEvent; }

    brls::VoidEvent* getSettingEvent() { return &this->settingEvent; }

    VideoProfile* getProfile() { return this->profile; }

    void hideVideoProgressSlider();
    void hideVideoQuality();

    static bool close();

private:
    /// OSD
    BRLS_BIND(brls::Label, titleLabel, "video/osd/title");
    BRLS_BIND(brls::Box, btnForward, "video/osd/forward");
    BRLS_BIND(brls::Box, btnBackward, "video/osd/backward");
    BRLS_BIND(brls::Box, btnSetting, "video/osd/setting");
    BRLS_BIND(brls::Box, btnCast, "video/osd/cast");
    BRLS_BIND(brls::Box, btnToggle, "video/osd/toggle");
    BRLS_BIND(brls::Box, btnVideoQuality, "video/quality/box");
    BRLS_BIND(brls::Box, btnVideoSpeed, "video/speed/box");
    BRLS_BIND(brls::Box, btnEpisode, "show/episode/box");
    BRLS_BIND(brls::Box, btnVolume, "video/osd/volume");
    BRLS_BIND(brls::Box, btnClose, "video/close/box");
    BRLS_BIND(brls::Box, osdLockBox, "video/osd/lock/box");
    BRLS_BIND(SVGImage, osdLockIcon, "video/osd/lock/icon");
    BRLS_BIND(SVGImage, btnToggleIcon, "video/osd/toggle/icon");
    BRLS_BIND(SVGImage, btnVolumeIcon, "video/osd/volume/icon");
    BRLS_BIND(SVGImage, btnDanmakuIcon, "video/osd/danmaku/icon");
    BRLS_BIND(SVGImage, btnDanmakuSettingIcon, "video/osd/danmaku/setting/icon");
    BRLS_BIND(brls::Box, osdTopBox, "video/osd/top/box");
    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");
    // 用于显示缓冲组件
    BRLS_BIND(brls::Box, osdCenterBox, "video/osd/center/box");
    BRLS_BIND(brls::Label, centerLabel, "video/osd/center/label");
    BRLS_BIND(brls::ProgressSpinner, osdSpinner, "video/osd/loading");
    // 用于通用的提示信息
    BRLS_BIND(brls::Box, osdInfoBox, "video/osd/info/box");
    BRLS_BIND(brls::Label, infoLabel, "video/osd/info/label");
    BRLS_BIND(SVGImage, infoIcon, "video/osd/info/icon");
    // 用于显示和控制视频时长
    BRLS_BIND(VideoProgressSlider, osdSlider, "video/osd/bottom/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "video/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "video/right/status");
    BRLS_BIND(brls::Label, videoSpeedLabel, "video/speed");
    BRLS_BIND(brls::Label, showEpisodeLabel, "show/episode");
    BRLS_BIND(brls::Label, speedHintLabel, "video/speed/hint/label");
    BRLS_BIND(brls::Box, speedHintBox, "video/speed/hint/box");
    BRLS_BIND(brls::Label, hintLabel, "video/osd/hint/label");
    BRLS_BIND(brls::Box, hintBox, "video/osd/hint/box");

    bool toggleDanmaku();
    void refreshDanmakuIcon();

    void registerMpvEvent();
    void unRegisterMpvEvent();

    void showLoading();
    void hideLoading(bool dimming = true);
    bool toggleProfile();
    /// OSD
    void toggleOSD();
    void showOSD(bool autoHide = true);
    void hideOSD();
    bool toggleOSDLock();
    bool toggleSpeed();
    bool toggleQuality();
    bool toggleVolume(brls::View* view);
    void showHint(const std::string& value);

    /// @brief 延迟 200ms 触发进度跳转到 seeking_range
    void requestSeeking(int seek, int delay = 400);
    void requestVolume(int value, int delay = 400);
    void requestBrightness(float value);
    void buttonProcessing();
    /// @brief notify videoview closed
    static void disableDimming(bool disable);

    int playIndex = -1;
    bool enableDanmaku = true;
    brls::Event<int> playIndexEvent;
    brls::VoidEvent settingEvent;

    // OSD
    bool isOsdShown = false;
    bool isOsdLock = false;
    brls::Time osdLastShowTime = 0;
    brls::Time hintLastShowTime = 0;
    brls::Time profileLastShowTime = 0;
    const brls::Time OSD_SHOW_TIME = 5000000;  //默认5秒
    OSDState osdState = OSDState::HIDDEN;
    VideoProfile* profile;

    int64_t seekingRange = 0;
    size_t seekingIter = 0;

    MPVEvent::Subscription eventSubscribeID;
    brls::Rect oldRect = brls::Rect(-1, -1, -1, -1);
    brls::InputManager* input = nullptr;

    size_t volumeIter = 0;  // 音量UI关闭的延迟函数 handle
    int volumeInit = 0;
    float brightnessInit = 0;
};
