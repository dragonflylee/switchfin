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
#include "utils/thread.hpp"
#include <curl/curl.h>
#include "view/mpv_core.hpp"
#include "view/selector_cell.hpp"
#include "api/analytics.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#ifdef __SWITCH__
#include "utils/overclock.hpp"
#endif

using namespace brls::literals;  // for _i18n

class SettingAbout : public brls::Box {
public:
    SettingAbout() {
        this->inflateFromXMLRes("xml/view/setting_about.xml");

        this->labelTitle->setText(AppVersion::getPackageName());
        this->labelVersion->setText(fmt::format("v{}-{} ({})", AppVersion::getVersion(), AppVersion::getCommit(),
#if defined(BOREALIS_USE_D3D11)
            "D3D11"
#elif defined(BOREALIS_USE_DEKO3D)
            "Deko3D"
#else
            "OpenGL"
#endif
            ));
        this->labelGithub->setText("https://github.com/" + AppVersion::git_repo);
        this->btnGithub->registerClickAction([this](...) {
            std::string url = this->labelGithub->getFullText();
            brls::Application::getPlatform()->openBrowser(url);
            return true;
        });
        this->btnGithub->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGithub));

        auto& mpv = MPVCore::instance();
        this->labelThirdpart->setText(fmt::format(
            "ffmpeg/{} {}\n{}", mpv.getString("ffmpeg-version"), mpv.getString("mpv-version"), curl_version()));
        brls::Logger::debug("dialog SettingAbout: create");
    }

    ~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }

private:
    BRLS_BIND(brls::Label, labelTitle, "setting/about/title");
    BRLS_BIND(brls::Label, labelVersion, "setting/about/version");
    BRLS_BIND(brls::Label, labelGithub, "setting/about/github");
    BRLS_BIND(brls::Label, labelThirdpart, "setting/about/thirdpart");
    BRLS_BIND(brls::Box, btnGithub, "setting/box/github");
};

SettingTab::SettingTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");
    GA("open_setting");
}

void SettingTab::hideStatus() { boxStatus->setVisibility(brls::Visibility::GONE); }

void SettingTab::onCreate() {
    auto& conf = AppConfig::instance();

    if (boxStatus->getVisibility() == brls::Visibility::VISIBLE) {
        btnServer->setDetailText(conf.getUrl());
        btnServer->registerClickAction([](...) {
            brls::Application::pushActivity(new ServerList(), brls::TransitionAnimation::NONE);
            return true;
        });

        btnUser->setDetailText(conf.getUser().name);
        btnUser->registerClickAction([](...) {
            Dialog::cancelable("main/setting/others/logout"_i18n, []() {
                brls::async([]() {
                    auto& c = AppConfig::instance();
                    HTTP::Header header = {"X-Emby-Token: " + c.getUser().access_token};
                    try {
                        HTTP::post(c.getUrl() + jellyfin::apiLogout, "", header, HTTP::Timeout{});
                        c.removeUser(c.getUser().id);
                    } catch (const std::exception& ex) {
                        brls::Logger::warning("Logout failed: {}", ex.what());
                    }
                    brls::sync([]() { brls::Application::quit(); });
                });
            });
            return true;
        });
    }

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

    auto& qualityOption = conf.getOptions(AppConfig::VIDEO_QUALITY);
    selectorQuality->init("main/setting/playback/video_quality"_i18n, qualityOption.options,
        conf.getValueIndex(AppConfig::VIDEO_QUALITY), [&qualityOption](int selected) {
            MPVCore::VIDEO_QUALITY = qualityOption.values[selected];
            AppConfig::instance().setItem(AppConfig::VIDEO_QUALITY, MPVCore::VIDEO_QUALITY);
        });

    auto& seekingOption = conf.getOptions(AppConfig::PLAYER_SEEKING_STEP);
    selectorSeeking->init("main/setting/playback/seeking_step"_i18n, seekingOption.options,
        conf.getValueIndex(AppConfig::PLAYER_SEEKING_STEP, 2), [&seekingOption](int selected) {
            MPVCore::SEEKING_STEP = seekingOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_SEEKING_STEP, MPVCore::SEEKING_STEP);
        });

    auto& inmemoryOption = conf.getOptions(AppConfig::PLAYER_INMEMORY_CACHE);
    selectorInmemory->init("main/setting/playback/in_memory_cache"_i18n, inmemoryOption.options,
        conf.getValueIndex(AppConfig::PLAYER_INMEMORY_CACHE, 1), [&inmemoryOption](int selected) {
            if (MPVCore::INMEMORY_CACHE == inmemoryOption.values[selected]) return;
            MPVCore::INMEMORY_CACHE = inmemoryOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_INMEMORY_CACHE, MPVCore::INMEMORY_CACHE);
            MPVCore::instance().restart();
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
            "main/setting/others/language/german"_i18n,
        },
        langIndex, [langIndex](int selected) {
            if (langIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& langOptions = conf.getOptions(AppConfig::APP_LANG);
            conf.setItem(AppConfig::APP_LANG, langOptions.options[selected]);
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
        });

    auto& threadOpt = conf.getOptions(AppConfig::REQUEST_THREADS);
    inputThreads->init("main/setting/network/threads"_i18n, threadOpt.options,
        conf.getValueIndex(AppConfig::REQUEST_THREADS, 3), [&threadOpt](int selected) {
            long threads = threadOpt.values[selected];
            ThreadPool::instance().start(threads);
            AppConfig::instance().setItem(AppConfig::REQUEST_THREADS, threads);
        });

    auto& timeoutOption = conf.getOptions(AppConfig::REQUEST_TIMEOUT);
    selectorTimeout->init("main/setting/network/timeout"_i18n, timeoutOption.options,
        conf.getValueIndex(AppConfig::REQUEST_TIMEOUT), [&timeoutOption](int selected) {
            HTTP::TIMEOUT = timeoutOption.values[selected];
            AppConfig::instance().setItem(AppConfig::REQUEST_TIMEOUT, HTTP::TIMEOUT);
        });

    bool proxyStatus = conf.getItem(AppConfig::HTTP_PROXY_STATUS, false);
    btnProxy->init("main/setting/network/proxy"_i18n, proxyStatus, [this](bool value) {
        inputProxyHost->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        inputProxyPort->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        HTTP::PROXY_STATUS = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY_STATUS, value);
    });

    inputProxyHost->init("main/setting/network/host"_i18n, HTTP::PROXY_HOST, [](std::string value) {
        HTTP::PROXY_HOST = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY_HOST, value);
    });
    inputProxyHost->setVisibility(proxyStatus ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    inputProxyPort->init("main/setting/network/port"_i18n, HTTP::PROXY_PORT, [](long value) {
        HTTP::PROXY_PORT = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY_PORT, value);
    });
    inputProxyPort->setVisibility(proxyStatus ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    btnDebug->init("main/setting/others/debug"_i18n, brls::Application::isDebuggingViewEnabled(), [](bool value) {
        brls::Application::enableDebuggingView(value);
        MPVCore::instance().restart();
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
