#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

PlayerSetting::PlayerSetting(const jellyfin::MediaSource& src, std::function<void()> reload) {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
    brls::Logger::debug("PlayerSetting: create");
    this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    this->subtitleTrack->detail->setVisibility(brls::Visibility::GONE);

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

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
        int64_t value = 0;
        mpv.get_property("sid", MPV_FORMAT_INT64, &value);
        this->subtitleTrack->init("main/player/subtitle"_i18n, subTrack, value, [&mpv](int selected) {
            VideoView::selectedSubtitle = selected;
            mpv.setInt("sid", selected);
        });
    } else if (subSource.size() > 0) {
        int value = 0;
        for (size_t i = 0; i < subStream.size(); i++)
            if (subStream[i] == VideoView::selectedSubtitle) value = i;
        this->subtitleTrack->init("main/player/subtitle"_i18n, subSource, value, [subStream, reload](int selected) {
            VideoView::selectedSubtitle = subStream[selected];
            reload();
        });
    } else {
        this->subtitleTrack->setVisibility(brls::Visibility::GONE);
    }
    // 音轨选择
    if (audioTrack.size() > 1) {
        int64_t value = 0;
        if (!mpv.get_property("aid", MPV_FORMAT_INT64, &value)) value -= 1;
        this->audioTrack->init("main/player/audio"_i18n, audioTrack, value, [&mpv](int selected) {
            VideoView::selectedAudio = selected + 1;
            mpv.setInt("aid", VideoView::selectedAudio);
        });
        this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    } else if (audioSource.size() > 1) {
        int value = 0;
        for (size_t i = 0; i < audioStream.size(); i++)
            if (audioStream[i] == VideoView::selectedAudio) value = i;
        this->audioTrack->init("main/player/audio"_i18n, audioSource, value, [audioStream, reload](int selected) {
            VideoView::selectedAudio = audioStream[selected];
            reload();
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
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
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
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("PlayerSetting: delete"); }
