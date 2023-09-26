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
#include "activity/server_list.hpp"
#include "activity/hint_activity.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/thread.hpp"
#include "view/mpv_core.hpp"
#include "api/analytics.hpp"
#include <curl/curl.h>
#ifdef __SWITCH__
#include "utils/overclock.hpp"
#endif

using namespace brls::literals;  // for _i18n

class SettingAbout : public brls::Box {
public:
    SettingAbout() {
        this->inflateFromXMLRes("xml/view/setting_about.xml");

        this->labelTitle->setText(AppVersion::getPackageName());
        this->labelVersion->setText(fmt::format("v{}-{}", AppVersion::getVersion(), AppVersion::getCommit()));
        this->labelGithub->setText("https://github.com/" + AppVersion::git_repo);
        this->btnGithub->registerClickAction([this](...){
            std::string url = this->labelGithub->getFullText();
            brls::Application::getPlatform()->openBrowser(url);
            return true;
        });
        this->btnGithub->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGithub));

        auto& mpv = MPVCore::instance();
        this->labelMPV->setText(fmt::format("ffmpeg/{} {}", mpv.ffmpeg_version, mpv.mpv_version));
        this->labelCurl->setText(curl_version());
        brls::Logger::debug("dialog SettingAbout: create");
    }

    ~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }

private:
    BRLS_BIND(brls::Label, labelTitle, "setting/about/title");
    BRLS_BIND(brls::Label, labelVersion, "setting/about/version");
    BRLS_BIND(brls::Label, labelGithub, "setting/about/github");
    BRLS_BIND(brls::Label, labelMPV, "setting/about/mpv");
    BRLS_BIND(brls::Label, labelCurl, "setting/about/curl");
    BRLS_BIND(brls::Box, btnGithub, "setting/box/github");
};

SettingTab::SettingTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");
    GA("open_setting");
}

void SettingTab::onCreate() {
    auto& conf = AppConfig::instance();

    btnServer->setDetailText(conf.getUrl());
    btnServer->registerClickAction([](...) {
        brls::Application::pushActivity(new ServerList(), brls::TransitionAnimation::NONE);
        return true;
    });

    btnUser->setDetailText(conf.getUser().name);

/// Hardware decode
#ifdef __SWITCH__
    btnOverClock->init(
        "main/setting/others/overclock"_i18n, conf.getItem(AppConfig::OVERCLOCK, false), [&conf](bool value) {
            SwitchSys::setClock(value);
            conf.setItem(AppConfig::OVERCLOCK, value);
        });
#else
    btnOverClock->setVisibility(brls::Visibility::GONE);
#endif

    btnHWDEC->init("main/setting/playback/hwdec"_i18n, MPVCore::HARDWARE_DEC, [&conf](bool value) {
        if (MPVCore::HARDWARE_DEC == value) return;
        MPVCore::HARDWARE_DEC = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_HWDEC, value);
    });

    btnDirectPlay->init("main/setting/playback/force_directplay"_i18n, MPVCore::FORCE_DIRECTPLAY, [&conf](bool value) {
        if (MPVCore::FORCE_DIRECTPLAY == value) return;
        MPVCore::FORCE_DIRECTPLAY = value;
        conf.setItem(AppConfig::FORCE_DIRECTPLAY, value);
    });

    auto& codecOption = conf.getOptions(AppConfig::TRANSCODEC);
    selectorCodec->init("main/setting/playback/transcodec"_i18n, {"AVC/H264", "HEVC/H265", "AV1"},
        conf.getOptionIndex(AppConfig::TRANSCODEC), [&codecOption](int selected) {
            MPVCore::VIDEO_CODEC = codecOption.options[selected];
            AppConfig::instance().setItem(AppConfig::TRANSCODEC, MPVCore::VIDEO_CODEC);
        });

    auto& bitRateOption = conf.getOptions(AppConfig::MAXBITRATE);
    selectorBitrate->init("main/setting/playback/maxbitrate"_i18n, bitRateOption.options,
        conf.getValueIndex(AppConfig::MAXBITRATE, bitRateOption.values.size() - 1), [&bitRateOption](int selected) {
            MPVCore::MAX_BITRATE[0] = bitRateOption.values[selected];
            AppConfig::instance().setItem(AppConfig::MAXBITRATE, bitRateOption.values[selected]);
        });

    auto& seekingOption = conf.getOptions(AppConfig::PLAYER_SEEKING_STEP);
    selectorSeeking->init("main/setting/playback/seeking_step"_i18n, seekingOption.options,
        conf.getValueIndex(AppConfig::PLAYER_SEEKING_STEP, 2), [&seekingOption](int selected) {
            MPVCore::SEEKING_STEP = seekingOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_SEEKING_STEP, MPVCore::SEEKING_STEP);
        });

    btnBottomBar->init(
        "main/setting/playback/bottom_bar"_i18n, conf.getItem(AppConfig::PLAYER_BOTTOM_BAR, true), [&conf](bool value) {
            MPVCore::BOTTOM_BAR = value;
            conf.setItem(AppConfig::PLAYER_BOTTOM_BAR, value);
        });

    btnOSDOnToggle->init(
        "main/setting/playback/osd_on_toggle"_i18n, conf.getItem(AppConfig::OSD_ON_TOGGLE, true), [&conf](bool value) {
            MPVCore::OSD_ON_TOGGLE = value;
            conf.setItem(AppConfig::OSD_ON_TOGGLE, value);
        });

#ifdef __SWITCH__
    btnTutorialOpenApp->registerClickAction([](...) -> bool {
        brls::Application::pushActivity(new HintActivity());
        return true;
    });
    btnTutorialError->registerClickAction([](...) -> bool {
        auto view = brls::View::createFromXMLResource("view/tutorial_error.xml");
        auto dialog = new brls::Dialog(dynamic_cast<brls::Box*>(view));
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });
    btnTutorialFont->setVisibility(brls::Visibility::GONE);
#else
    btnTutorialFont->registerClickAction([](...) -> bool {
        auto view = brls::View::createFromXMLResource("view/tutorial_font.xml");
        auto dialog = new brls::Dialog(dynamic_cast<brls::Box*>(view));
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });
    btnTutorialOpenApp->setVisibility(brls::Visibility::GONE);
    btnTutorialError->setVisibility(brls::Visibility::GONE);
#endif

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
    selectorKeymap->init(
        "main/setting/others/keymap/header"_i18n,
        {
            "main/setting/others/keymap/xbox"_i18n,
            "main/setting/others/keymap/ps"_i18n,
            "main/setting/others/keymap/keyboard"_i18n,
        },
        keyIndex,
        [keyIndex](int selected) {
            if (keyIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& keyOptions = conf.getOptions(AppConfig::KEYMAP);
            conf.setItem(AppConfig::KEYMAP, keyOptions.options[selected]);
        },
        [](...) { Dialog::quitApp(); });
#else
    selectorKeymap->setVisibility(brls::Visibility::GONE);
#endif

    // App language
    int langIndex = conf.getOptionIndex(AppConfig::APP_LANG);
    selectorLang->init(
        "main/setting/others/language/header"_i18n,
        {
            "main/setting/others/language/auto"_i18n,
            "main/setting/others/language/english"_i18n,
            "main/setting/others/language/chinese_s"_i18n,
            "main/setting/others/language/chinese_t"_i18n,
            "main/setting/others/language/german"_i18n,
        },
        langIndex,
        [langIndex](int selected) {
            if (langIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& langOptions = conf.getOptions(AppConfig::APP_LANG);
            conf.setItem(AppConfig::APP_LANG, langOptions.options[selected]);
        },
        [](...) { Dialog::quitApp(); });

    // App theme
    int themeIndex = conf.getOptionIndex(AppConfig::APP_THEME);
    selectorTheme->init(
        "main/setting/others/theme/header"_i18n,
        {
            "main/setting/others/theme/1"_i18n,
            "main/setting/others/theme/2"_i18n,
            "main/setting/others/theme/3"_i18n,
        },
        themeIndex,
        [themeIndex](int selected) {
            if (themeIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& themeOptions = conf.getOptions(AppConfig::APP_THEME);
            conf.setItem(AppConfig::APP_THEME, themeOptions.options[selected]);
        },
        [](...) { Dialog::quitApp(); });

    inputThreads->init(
        "main/setting/network/threads"_i18n, ThreadPool::instance().size(),
        [](long threads) {
            ThreadPool::instance().start(threads);
            AppConfig::instance().setItem(AppConfig::REQUEST_THREADS, threads);
        },
        "", 1);

    auto& timeoutOption = conf.getOptions(AppConfig::REQUEST_TIMEOUT);
    selectorTimeout->init("main/setting/network/timeout"_i18n, timeoutOption.options,
        conf.getValueIndex(AppConfig::REQUEST_TIMEOUT), [&timeoutOption](int selected) {
            AppConfig::instance().setItem(AppConfig::REQUEST_TIMEOUT, timeoutOption.values[selected]);
        });

    btnReleaseChecker->title->setText(
        fmt::format("{} ({}: {})", "main/setting/others/release"_i18n, "hints/current"_i18n, AppVersion::getVersion()));
    btnReleaseChecker->registerClickAction([](...) -> bool {
        AppVersion::checkUpdate(0, true);
        return true;
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
