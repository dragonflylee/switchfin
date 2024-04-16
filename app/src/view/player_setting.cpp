#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/button_close.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

PlayerSetting::PlayerSetting(const jellyfin::MediaSource& src) {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
    brls::Logger::debug("PlayerSetting: create");
    this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    this->subtitleTrack->detail->setVisibility(brls::Visibility::GONE);

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    auto& mpv = MPVCore::instance();

    std::vector<std::string> audioTrack, audioSource;
    std::vector<int> audioStream;
    std::vector<std::string> subTrack = {"main/player/none"_i18n};
    std::vector<std::string> subSource = {"main/player/none"_i18n};
    std::vector<int> subStream = {0};

    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        std::string type = mpv.getString(fmt::format("track-list/{}/type", n));
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
        if (title.empty()) title = fmt::format("{} track {}", type, n);
        if (type == "sub")
            subTrack.push_back(title);
        else if (type == "audio")
            audioTrack.push_back(title);
    }

    for (auto& s : src.MediaStreams) {
        if (s.Type == jellyfin::streamTypeAudio) {
            audioSource.push_back(s.DisplayTitle);
            audioStream.push_back(s.Index);
        } else if (s.Type == jellyfin::streamTypeSubtitle) {
            subSource.push_back(s.DisplayTitle);
            subStream.push_back(s.Index);
        }
    }
    // 字幕选择
    if (subTrack.size() > 1) {
        int64_t value = mpv.getInt("sid");
        this->subtitleTrack->init("main/player/subtitle"_i18n, subTrack, value, [&mpv](int selected) {
            selectedSubtitle = selected;
            mpv.setInt("sid", selected);
        });
    } else if (subSource.size() > 1) {
        int value = 0;
        for (size_t i = 0; i < subStream.size(); i++)
            if (subStream[i] == selectedSubtitle) value = i;
        this->subtitleTrack->init("main/player/subtitle"_i18n, subSource, value, [subStream](int selected) {
            selectedSubtitle = subStream[selected];
            MPVCore::instance().getCustomEvent()->fire(VideoView::QUALITY_CHANGE, nullptr);
        });
    } else {
        this->subtitleTrack->setVisibility(brls::Visibility::GONE);
    }
    // 音轨选择
    if (audioTrack.size() > 1) {
        int64_t value = mpv.getInt("aid", 1) - 1;
        this->audioTrack->init("main/player/audio"_i18n, audioTrack, value, [&mpv](int selected) {
            selectedAudio = selected + 1;
            mpv.setInt("aid", selectedAudio);
        });
        this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    } else if (audioSource.size() > 1) {
        int value = 0;
        for (size_t i = 0; i < audioStream.size(); i++)
            if (audioStream[i] == selectedAudio) value = i;
        this->audioTrack->init("main/player/audio"_i18n, audioSource, value, [audioStream](int selected) {
            selectedAudio = audioStream[selected];
            MPVCore::instance().getCustomEvent()->fire(VideoView::QUALITY_CHANGE, nullptr);
        });
    } else {
        this->audioTrack->setVisibility(brls::Visibility::GONE);
    }

    auto& conf = AppConfig::instance();
    auto& seekingOption = conf.getOptions(AppConfig::PLAYER_SEEKING_STEP);
    seekingStep->init("main/setting/playback/seeking_step"_i18n, seekingOption.options,
        conf.getValueIndex(AppConfig::PLAYER_SEEKING_STEP, 2), [&seekingOption](int selected) {
            MPVCore::SEEKING_STEP = seekingOption.values[selected];
            AppConfig::instance().setItem(AppConfig::PLAYER_SEEKING_STEP, MPVCore::SEEKING_STEP);
        });

/// Fullscreen
#if defined(__linux__) || defined(_WIN32)
    btnFullscreen->init(
        "main/setting/others/fullscreen"_i18n, conf.getItem(AppConfig::FULLSCREEN, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, value);
            // 设置当前状态
            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
        });

    btnAlwaysOnTop->init(
        "main/setting/others/always_on_top"_i18n, conf.getItem(AppConfig::ALWAYS_ON_TOP, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::ALWAYS_ON_TOP, value);
            brls::Application::getPlatform()->setWindowAlwaysOnTop(value);
        });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnAlwaysOnTop->setVisibility(brls::Visibility::GONE);
#endif

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

    /// Player mirror
    btnVideoMirror->init("main/setting/filter/mirror"_i18n,
        {
            "hints/off"_i18n,
            "main/setting/filter/hflip"_i18n,
            "main/setting/filter/vflip"_i18n,
        },
        MPVCore::VIDEO_FILTER, [&mpv](int value) {
            MPVCore::VIDEO_FILTER = value;
            switch (value) {
            case 1:
                mpv.command("set", "vf", "hflip");
                break;
            case 2:
                mpv.command("set", "vf", "vflip");
                break;
            default:
                mpv.command("set", "vf", "");
            }
            // 如果正在使用硬解，那么将硬解更新为 auto-copy，避免直接硬解因为不经过 cpu 处理导致镜像翻转无效
            if (MPVCore::HARDWARE_DEC) {
                const char* hwdec = value > 0 ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD.c_str();
                mpv.command("set", "hwdec", hwdec);
                brls::Logger::info("MPV hardware decode: {}", hwdec);
            }
        });

    /// Player aspect
    btnVideoAspect->init("main/setting/aspect/header"_i18n,
        {
            "main/setting/aspect/auto"_i18n,
            "main/setting/aspect/stretch"_i18n,
            "main/setting/aspect/crop"_i18n,
            "4:3",
            "16:9",
        },
        conf.getOptionIndex(AppConfig::PLAYER_ASPECT), [&mpv, &conf](int value) {
            auto& opt = conf.getOptions(AppConfig::PLAYER_ASPECT);
            MPVCore::VIDEO_ASPECT = opt.options.at(value);
            mpv.setAspect(MPVCore::VIDEO_ASPECT);
            conf.setItem(AppConfig::PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);
        });

    /// Subsync
    double subDelay = mpv.getDouble("sub-delay");
    btnSubsync->setDetailText(fmt::format("{:.1f}", subDelay));
    btnSubsync->detail->setWidth(50);
    btnSubsync->title->setWidth(116);
    btnSubsync->title->setMarginRight(0);
    btnSubsync->slider->setStep(0.05f);
    btnSubsync->slider->setMarginRight(0);
    btnSubsync->slider->setPointerSize(20);
    btnSubsync->init("main/setting/playback/subsync"_i18n, (subDelay + 10) * 0.05f, [this](float value) {
        float data = value * 20 - 10.f;
        MPVCore::instance().setDouble("sub-delay", data);
        btnSubsync->setDetailText(fmt::format("{:.1f}", data));
    });
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("PlayerSetting: delete"); }
