#include "view/player_setting.hpp"
#include "view/mpv_core.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

PlayerSetting::PlayerSetting(const jellyfin::MediaSource& src) {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
    brls::Logger::debug("PlayerSetting: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

    auto& mpv = MPVCore::instance();

    std::vector<std::string> subOpts, audOpts;
    subOpts.push_back("main/player/none"_i18n);

    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        std::string type = mpv.getString(fmt::format("track-list/{}/type", n));
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (type == "sub") subOpts.push_back(title);
        else if (type == "audio") audOpts.push_back(title);
    }
    // 字幕选择
    if (subOpts.size() > 1) {
        int64_t value = 0;
        mpv.get_property("sid", MPV_FORMAT_INT64, &value);
        this->subtitleTrack->init("main/player/subtitle"_i18n, subOpts, value,
            [&mpv](int selected) { mpv.setInt("sid", selected); });
        this->subtitleTrack->detail->setVisibility(brls::Visibility::GONE);
    } else {
        this->subtitleTrack->setVisibility(brls::Visibility::GONE);
    }
    // 音轨选择
    if (audOpts.size() > 1) {
        int64_t value = 0;
        if (!mpv.get_property("aid", MPV_FORMAT_INT64, &value)) value -= 1;
        this->audioTrack->init("main/player/audio"_i18n, audOpts, value,
            [&mpv](int selected) { mpv.setInt("aid", selected + 1); });
        this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
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
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("PlayerSetting: delete"); }
