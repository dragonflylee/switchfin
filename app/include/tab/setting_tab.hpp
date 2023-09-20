/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include "view/auto_tab_frame.hpp"

class SettingTab : public AttachedView {
public:
    SettingTab();

    void onCreate() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::RadioCell, btnTutorialOpenApp, "tools/tutorial_open");
    BRLS_BIND(brls::RadioCell, btnTutorialError, "tools/tutorial_error");
    BRLS_BIND(brls::RadioCell, btnTutorialFont, "tools/tutorial_font");
    BRLS_BIND(brls::DetailCell, btnUser, "setting/user");
    BRLS_BIND(brls::DetailCell, btnServer, "setting/server");
    BRLS_BIND(brls::BooleanCell, btnHWDEC, "setting/video/hwdec");
    BRLS_BIND(brls::BooleanCell, btnDirectPlay, "setting/video/directplay");
    BRLS_BIND(brls::SelectorCell, selectorCodec, "setting/video/codec");
    BRLS_BIND(brls::SelectorCell, selectorBitrate, "setting/video/bitrate");
    BRLS_BIND(brls::SelectorCell, selectorSeeking, "setting/player/seeking");
    BRLS_BIND(brls::BooleanCell, btnBottomBar, "setting/player/bottom_bar");
    BRLS_BIND(brls::BooleanCell, btnOSDOnToggle, "setting/player/osd_on_toggle");
    BRLS_BIND(brls::BooleanCell, btnFullscreen, "setting/fullscreen");
    BRLS_BIND(brls::BooleanCell, btnOverClock, "setting/overclock");
    BRLS_BIND(brls::InputNumericCell, inputThreads, "setting/network/threads");
    BRLS_BIND(brls::SelectorCell, selectorTimeout, "setting/network/timeout");
    BRLS_BIND(brls::SelectorCell, selectorKeymap, "setting/keymap");
    BRLS_BIND(brls::SelectorCell, selectorLang, "setting/language");
    BRLS_BIND(brls::SelectorCell, selectorTheme, "setting/ui/theme");
    BRLS_BIND(brls::RadioCell, btnReleaseChecker, "setting/release_checker");
    BRLS_BIND(brls::DetailCell, btnAbout, "setting/about");
};
