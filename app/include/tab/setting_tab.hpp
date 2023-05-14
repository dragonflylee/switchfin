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

#include <borealis.hpp>

class SettingTab : public brls::Box {
public:
    SettingTab();

    static brls::View* create();

private:
    BRLS_BIND(brls::BooleanCell, btnHWDEC, "setting/video/hwdec");
    BRLS_BIND(brls::SelectorCell, selectorCodec, "setting/video/codec");
    BRLS_BIND(brls::BooleanCell, btnFullscreen, "setting/fullscreen");
    BRLS_BIND(brls::SelectorCell, selectorKeymap, "setting/keymap");
    BRLS_BIND(brls::SelectorCell, selectorLang, "setting/language");
    BRLS_BIND(brls::SelectorCell, selectorTheme, "setting/ui/theme");
    BRLS_BIND(brls::DetailCell, btnAbout, "setting/about");

    bool showAbout(brls::View* view);
};
