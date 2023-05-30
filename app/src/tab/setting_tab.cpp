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

#include "tab/setting_tab.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "view/mpv_core.hpp"

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <borealis/platforms/desktop/desktop_platform.hpp>
#endif

using namespace brls::literals;  // for _i18n

class SettingAbout : public brls::Box {
public:
    SettingAbout() {
        this->inflateFromXMLRes("xml/view/setting_about.xml");
        this->labelTitle->setText(AppVersion::getPlatform());
        this->labelVersion->setText(AppVersion::getVersion());
        brls::Logger::debug("dialog SettingAbout: create");
    }

    ~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }

private:
    BRLS_BIND(brls::Label, labelTitle, "setting/about/title");
    BRLS_BIND(brls::Label, labelVersion, "setting/about/version");
};

SettingTab::SettingTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    auto& conf = AppConfig::instance();

    btnServer->setDetailText(conf.getServerUrl());
    btnUser->setDetailText(conf.getUsername());

/// Hardware decode
#ifdef __SWITCH__
    btnHWDEC->setVisibility(brls::Visibility::GONE);
#else
    btnHWDEC->init("main/setting/playback/hwdec"_i18n, MPVCore::HARDWARE_DEC, [&conf](bool value) {
        if (MPVCore::HARDWARE_DEC == value) return;
        MPVCore::HARDWARE_DEC = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_HWDEC, value);
    });
#endif

    auto& codecOption = conf.getOptions(AppConfig::VIDEO_CODEC);
    selectorCodec->init("main/setting/playback/video_codec"_i18n, codecOption.options,
        conf.getOptionIndex(AppConfig::VIDEO_CODEC), [&codecOption](int selected) {
            AppConfig::instance().setItem(AppConfig::VIDEO_CODEC, codecOption.options[selected]);
        });

/// Fullscreen
#if defined(__linux__) || defined(_WIN32)
    btnFullscreen->init(
        "main/setting/others/fullscreen"_i18n, conf.getItem(AppConfig::FULLSCREEN, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, value);
            // 设置当前状态
            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
        });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    int keyIndex = conf.getOptionIndex(AppConfig::KEYMAP);
    selectorKeymap->init("main/setting/others/keymap/header"_i18n,
        {
            "main/setting/others/keymap/xbox"_i18n,
            "main/setting/others/keymap/ps"_i18n,
            "main/setting/others/keymap/keyboard"_i18n,
        },
        keyIndex, [keyIndex](int selected) {
            if (keyIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& keyOptions = conf.getOptions(AppConfig::KEYMAP);
            conf.setItem(AppConfig::KEYMAP, keyOptions.options[selected]);
            Dialog::quitApp();
        });
#else
    selectorKeymap->setVisibility(brls::Visibility::GONE);
#endif

    // App language
    int langIndex = conf.getOptionIndex(AppConfig::APP_LANG);
    selectorLang->init("main/setting/others/language/header"_i18n,
        {
            "main/setting/others/language/auto"_i18n,
            "main/setting/others/language/english"_i18n,
            "main/setting/others/language/chinese_s"_i18n,
            "main/setting/others/language/chinese_t"_i18n,
        },
        langIndex, [langIndex](int selected) {
            if (langIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& langOptions = conf.getOptions(AppConfig::APP_LANG);
            conf.setItem(AppConfig::APP_LANG, langOptions.options[selected]);
            Dialog::quitApp();
        });

    // App theme
    int themeIndex = conf.getOptionIndex(AppConfig::APP_THEME);
    selectorTheme->init("main/setting/others/theme/header"_i18n,
        {
            "main/setting/others/theme/1"_i18n,
            "main/setting/others/theme/2"_i18n,
            "main/setting/others/theme/3"_i18n,
        },
        themeIndex, [themeIndex](int selected) {
            if (themeIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& themeOptions = conf.getOptions(AppConfig::APP_THEME);
            conf.setItem(AppConfig::APP_THEME, themeOptions.options[selected]);
            Dialog::quitApp();
        });

    selectorTexture->init("main/setting/image/texture"_i18n,
        {"100", "200 (" + "hints/preset"_i18n + ")", "300", "400", "500"},
        conf.getItem(AppConfig::TEXTURE_CACHE_NUM, 200) / 100 - 1, [](int selected) {
            int num = 100 * (selected + 1);
            AppConfig::instance().setItem(AppConfig::TEXTURE_CACHE_NUM, num);
        });

    int threads = conf.getItem(AppConfig::REQUEST_THREADS, 1);
    selectorThreads->init("main/setting/image/threads"_i18n, {"1", "2", "4", "8"}, log2(threads), [](int selected) {
        AppConfig::instance().setItem(AppConfig::REQUEST_THREADS, 1 << selected);
    });

    btnAbout->setDetailText(">");
    btnAbout->registerClickAction([](...) {
        brls::Dialog* dialog = new brls::Dialog(new SettingAbout());
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });
}

brls::View* SettingTab::create() { return new SettingTab(); }