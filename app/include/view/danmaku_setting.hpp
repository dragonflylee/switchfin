//
// Created by fang on 2023/1/10.
//

// register this fragment in main.cpp
//#include "fragment/player_danmaku_setting.hpp"
//    brls::Application::registerXMLView("DanmakuSetting", DanmakuSetting::create);
// <brls:View xml=@res/xml/fragment/player_danmaku_setting.xml

#pragma once

#include <borealis.hpp>

class ButtonClose;

class DanmakuSetting : public brls::Box {
public:
    DanmakuSetting();

    bool isTranslucent() override;

    View* getDefaultFocus() override;

    ~DanmakuSetting() override;

    static View* create();

private:
    BRLS_BIND(brls::SelectorCell, cellArea, "player/danmaku/style/area");
    BRLS_BIND(brls::SelectorCell, cellAlpha, "player/danmaku/style/alpha");
    BRLS_BIND(brls::SelectorCell, cellSpeed, "player/danmaku/style/speed");
    BRLS_BIND(brls::SelectorCell, cellFontsize, "player/danmaku/style/fontsize");
    BRLS_BIND(brls::SelectorCell, cellLineHeight, "player/danmaku/style/lineHeight");
    BRLS_BIND(brls::SelectorCell, cellBackground, "player/danmaku/style/background");

    BRLS_BIND(brls::SelectorCell, cellRenderPerf, "player/danmaku/performance/render");

    BRLS_BIND(ButtonClose, closebtn, "button/close");
    BRLS_BIND(brls::ScrollingFrame, settings, "danmaku/settings");
    BRLS_BIND(brls::Box, cancel, "player/cancel");
};