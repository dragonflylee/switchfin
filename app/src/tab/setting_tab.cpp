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
#include "tab/dashboard.hpp"
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
#ifdef __linux__
#include <borealis/platforms/desktop/steam_deck.hpp>
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
#elif defined(BOREALIS_USE_GXM)
            "GXM"
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
        int libass = mpv.getInt("libass-version");
        this->labelThirdpart->setText(fmt::format("{} ffmpeg/{} libass/{:x}.{:x}.{:x}\n{}",
            mpv.getString("mpv-version"), mpv.getString("ffmpeg-version"), (libass >> 28) & 0xf,
            (libass >> 20) & 0xfffff, (libass >> 12) & 0xff, curl_version()));
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

class TutorialFont : public brls::Box {
public:
    TutorialFont() {
        const std::string confDir = AppConfig::instance().configDir();
        this->inflateFromXMLRes("xml/view/tutorial_font.xml");
        fontA3->setText(fmt::format(fmt::runtime("main/setting/tutorial/font_a3"_i18n), confDir));
    }

private:
    BRLS_BIND(brls::Label, fontA3, "tutorial/font_a3");
};

SettingTab::SettingTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");
    GA("open_setting");

    this->registerBoolXMLAttribute("hideStatus", [this](bool value) {
        auto visibility = value ? brls::Visibility::GONE : brls::Visibility::VISIBLE;
        this->boxStatus->setVisibility(visibility);
        this->btnServer->setVisibility(visibility);
        this->btnUser->setVisibility(visibility);
        this->btnDashboard->setVisibility(visibility);
    });
}

void SettingTab::onCreate() {
    auto& conf = AppConfig::instance();

    if (boxStatus->getVisibility() == brls::Visibility::VISIBLE) {
        btnServer->setDetailText(conf.getUrl());
        btnServer->registerClickAction([](...) {
            brls::Application::pushActivity(new ServerList(), brls::TransitionAnimation::NONE);
            return true;
        });

        btnUser->setDetailText(conf.getUserName());
        btnUser->registerClickAction([](...) {
            Dialog::cancelable("main/setting/others/logout"_i18n, []() {
                brls::async([]() {
                    auto& c = AppConfig::instance();
                    HTTP::Header header = {c.getAuth(c.getToken())};
                    try {
                        HTTP::post(c.getUrl() + jellyfin::apiLogout, "", header, HTTP::Timeout{});
                        c.removeUser(c.getUserId());
                    } catch (const std::exception& ex) {
                        brls::Logger::warning("Logout failed: {}", ex.what());
                    }
                    brls::sync([]() { brls::Application::quit(); });
                });
            });
            return true;
        });

        if (conf.isAdmin()) {
            this->btnDashboard->registerClickAction([this](...) {
                this->present(new Dashboard());
                return true;
            });
        } else {
            this->btnDashboard->setVisibility(brls::Visibility::GONE);
        }
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

/// Hardware decode
#ifdef PS4
    btnHWDEC->setVisibility(brls::Visibility::GONE);
#else
    btnHWDEC->init("main/setting/playback/hwdec"_i18n, MPVCore::HARDWARE_DEC, [&conf](bool value) {
        if (MPVCore::HARDWARE_DEC == value) return;
        MPVCore::HARDWARE_DEC = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_HWDEC, value);
    });
#endif

#if defined(ANDROID)
    auto& voOption = conf.getOptions(AppConfig::MPV_VO);
    selectorVO->init("main/setting/playback/vo"_i18n, voOption.options, conf.getOptionIndex(AppConfig::MPV_VO),
        [&voOption](int selected) {
            if (MPVCore::VO == voOption.options[selected]) return;
            MPVCore::VO = voOption.options[selected];
            AppConfig::instance().setItem(AppConfig::MPV_VO, MPVCore::VO);
            MPVCore::instance().restart();
        });
#else
    selectorVO->setVisibility(brls::Visibility::GONE);
#endif

    /// Decode quality
    btnQuality->init("main/setting/playback/low_quality"_i18n, MPVCore::LOW_QUALITY, [&conf](bool value) {
        if (MPVCore::LOW_QUALITY == value) return;
        MPVCore::LOW_QUALITY = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_LOW_QUALITY, value);
    });

    btnSubFallback->init("main/setting/playback/subs_fallback"_i18n, MPVCore::SUBS_FALLBACK, [&conf](bool value) {
        if (MPVCore::SUBS_FALLBACK == value) return;
        MPVCore::SUBS_FALLBACK = value;
        MPVCore::instance().restart();
        conf.setItem(AppConfig::PLAYER_SUBS_FALLBACK, value);
    });

    btnDirectPlay->init("main/setting/playback/force_directplay"_i18n, MPVCore::FORCE_DIRECTPLAY, [&conf](bool value) {
        if (MPVCore::FORCE_DIRECTPLAY == value) return;
        MPVCore::FORCE_DIRECTPLAY = value;
        conf.setItem(AppConfig::FORCE_DIRECTPLAY, value);
    });

#if defined(__PSV__)
    selectorCodec->setVisibility(brls::Visibility::GONE);
#else
    auto& codecOption = conf.getOptions(AppConfig::TRANSCODEC);
    selectorCodec->init("main/setting/playback/transcodec"_i18n, {"AVC/H264", "HEVC/H265", "AV1"},
        conf.getOptionIndex(AppConfig::TRANSCODEC), [&codecOption](int selected) {
            MPVCore::VIDEO_CODEC = codecOption.options[selected];
            AppConfig::instance().setItem(AppConfig::TRANSCODEC, MPVCore::VIDEO_CODEC);
        });
#endif

#if defined(__PS4__) || defined(__PSV__) || defined(TRIMUI)
    selectorAudioChannels->setVisibility(brls::Visibility::GONE);
#else
    auto& audioChannelsOption = conf.getOptions(AppConfig::AUDIO_CHANNELS);
    selectorAudioChannels->init("main/setting/playback/audio_channels"_i18n, {"Auto", "Stereo", "Mono"},
        conf.getOptionIndex(AppConfig::AUDIO_CHANNELS), [&audioChannelsOption](int selected) {
            MPVCore::AUDIO_CHANNELS = audioChannelsOption.options[selected];
            AppConfig::instance().setItem(AppConfig::AUDIO_CHANNELS, MPVCore::AUDIO_CHANNELS);
            MPVCore::instance().restart();
        });
#endif

    auto& inmemoryOption = conf.getOptions(AppConfig::PLAYER_INMEMORY_CACHE);
    selectorInmemory->init("main/setting/playback/in_memory_cache"_i18n, inmemoryOption.options,
        conf.getValueIndex(AppConfig::PLAYER_INMEMORY_CACHE, 1), [&inmemoryOption](int selected) {
            if (MPVCore::INMEMORY_CACHE == inmemoryOption.values[selected]) return;
            MPVCore::INMEMORY_CACHE = inmemoryOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_INMEMORY_CACHE, MPVCore::INMEMORY_CACHE);
            MPVCore::instance().restart();
        });

    btnBottomBar->init("main/setting/playback/bottom_bar"_i18n, MPVCore::BOTTOM_BAR, [&conf](bool value) {
        MPVCore::BOTTOM_BAR = value;
        conf.setItem(AppConfig::PLAYER_BOTTOM_BAR, value);
    });

    btnShowFPS->init("main/setting/ui/show_fps"_i18n, brls::Application::getFPSStatus(), [&conf](bool value) {
        brls::Application::setFPSStatus(value);
        conf.setItem(AppConfig::SHOW_FPS, value);
    });

    int scaleIndex = conf.getOptionIndex(AppConfig::APP_UI_SCALE, 1);
    selectorScale->init("main/setting/ui/scale/header"_i18n,
        {
            "main/setting/ui/scale/544p"_i18n,
            "main/setting/ui/scale/720p"_i18n,
            "main/setting/ui/scale/900p"_i18n,
            "main/setting/ui/scale/1080p"_i18n,
        },
        scaleIndex, [scaleIndex](int selected) {
            if (scaleIndex == selected) return;
            auto& conf = AppConfig::instance();
            auto& scaleOption = conf.getOptions(AppConfig::APP_UI_SCALE);
            conf.setItem(AppConfig::APP_UI_SCALE, scaleOption.options[selected]);
        });

    selectorVSync->init("main/setting/ui/vsync"_i18n, {"hints/off"_i18n, "hints/on"_i18n, "1/2", "1/3", "1/4"},
        VideoContext::swapInterval, [&conf](int selected) {
            if (selected == VideoContext::swapInterval) return;
            brls::Application::setSwapInterval(selected);
            conf.setItem(AppConfig::SWAP_INTERVAL, selected);
        });

    btnOSDOnToggle->init("main/setting/playback/osd_on_toggle"_i18n, MPVCore::OSD_ON_TOGGLE, [&conf](bool value) {
        MPVCore::OSD_ON_TOGGLE = value;
        conf.setItem(AppConfig::OSD_ON_TOGGLE, value);
    });

    btnTouchGesture->init("main/setting/playback/touch_gesture"_i18n, MPVCore::TOUCH_GESTURE, [&conf](bool value) {
        MPVCore::TOUCH_GESTURE = value;
        conf.setItem(AppConfig::TOUCH_GESTURE, value);
    });

    btnTvOsdMode->init("main/setting/control/tv_osd"_i18n, MPVCore::OSD_TV_MODE, [&conf](bool value) {
        MPVCore::OSD_TV_MODE = value;
        conf.setItem(AppConfig::PLAYER_TV_MODE, value);
    });

    btnClipPoint->init("main/setting/playback/clip_point"_i18n, MPVCore::CLIP_POINT, [&conf](bool value) {
        MPVCore::CLIP_POINT = value;
        conf.setItem(AppConfig::CLIP_POINT, value);
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
#else
    btnTutorialOpenApp->setVisibility(brls::Visibility::GONE);
    btnTutorialError->setVisibility(brls::Visibility::GONE);
#endif
    btnTutorialFont->registerClickAction([](...) -> bool {
        auto dialog = new brls::Dialog(new TutorialFont());
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return true;
    });

    btnOpenConfig->registerClickAction([](...) -> bool {
        const std::string confDir = AppConfig::instance().configDir();
#if defined(__SWITCH__) || defined(__PSV__) || defined(__PS4__) || defined(ANDROID)
        Dialog::show("main/setting/others/config_dir"_i18n + ":\n" + confDir);
#else
#ifdef __linux__
        if (!brls::isSteamDeck())
#endif
        {
            brls::Application::getPlatform()->openBrowser(confDir);
        }
#endif
        return true;
    });

/// Fullscreen
#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID) && !defined(TRIMUI)
    btnFullscreen->init(
        "main/setting/others/fullscreen"_i18n, conf.getItem(AppConfig::FULLSCREEN, false), [](bool value) {
            VideoContext::FULLSCREEN = value;
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, value);
            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
        });

    btnAlwaysOnTop->init(
        "main/setting/others/always_on_top"_i18n, conf.getItem(AppConfig::ALWAYS_ON_TOP, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::ALWAYS_ON_TOP, value);
            brls::Application::getPlatform()->setWindowAlwaysOnTop(value);
        });

    btnSingle->init("main/setting/others/single"_i18n, conf.getItem(AppConfig::SINGLE, false), [](bool value) {
        AppConfig::instance().setItem(AppConfig::SINGLE, value);
        MPVCore::instance().restart();
    });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnAlwaysOnTop->setVisibility(brls::Visibility::GONE);
    btnSingle->setVisibility(brls::Visibility::GONE);
#endif

#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(TRIMUI)
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
            "English",
            "简体中文",
            "繁體中文",
            "日本語",
            "한국어",
            "Русский",
            "Deutsch",
            "Français",
            "Español",
            "Português",
            "Czech",
            "Українська",
            "Türkçe",
            "Tiếng việt",
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
    auto thIt = std::find(threadOpt.values.begin(), threadOpt.values.end(), ThreadPool::max_thread_num);
    size_t thIndex = thIt != threadOpt.values.end() ? thIt - threadOpt.values.begin() : 0;
    inputThreads->init("main/setting/network/threads"_i18n, threadOpt.options,
        conf.getValueIndex(AppConfig::REQUEST_THREADS, thIndex), [&threadOpt](int selected) {
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

    btnProxy->init("main/setting/network/proxy"_i18n, HTTP::PROXY_STATUS, [this](bool value) {
        inputProxy->setVisibility(value ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        HTTP::PROXY_STATUS = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY_STATUS, value);
    });

    inputProxy->init("main/setting/network/host"_i18n, HTTP::PROXY, [](std::string value) {
        if (value.find_first_of("://") == std::string::npos) value = "http://" + value;
        HTTP::PROXY = value;
        AppConfig::instance().setItem(AppConfig::HTTP_PROXY, value);
    });
    inputProxy->setVisibility(HTTP::PROXY_STATUS ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    auto& dlQualityOpt = conf.getOptions(AppConfig::DOWNLOAD_QUALITY);
    selectorDownloadQuality->init("main/download/quality"_i18n,
        {"main/download/original"_i18n, "1080p", "720p", "480p"},
        conf.getValueIndex(AppConfig::DOWNLOAD_QUALITY), [&dlQualityOpt](int selected) {
            AppConfig::instance().setItem(AppConfig::DOWNLOAD_QUALITY, dlQualityOpt.values[selected]);
        });

    btnSync->init("main/setting/others/sync"_i18n, AppConfig::SYNC, [](bool value) {
        AppConfig::SYNC = value;
        AppConfig::instance().setItem(AppConfig::SYNC_SETTING, value);
    });

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
