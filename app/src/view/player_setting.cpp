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

    std::vector<std::string> subtitleOption, audioOption;
    for (auto& it : src.MediaStreams) {
        if (it.Type == jellyfin::streamTypeSubtitle) {
            subtitleOption.push_back(it.DisplayTitle);
        } else if (it.Type == jellyfin::streamTypeAudio) {
            audioOption.push_back(it.DisplayTitle);
        }
    }
    // 字幕选择
    if (subtitleOption.size() > 0) {
        int64_t value = 0;
        MPVCore::instance().get_property("sid", MPV_FORMAT_INT64, &value);
        this->subtitleTrack->init("main/player/subtitle"_i18n, subtitleOption, int(value - 1),
            [](int selected) { MPVCore::instance().set_property("sid", selected + 1); });
        this->subtitleTrack->detail->setVisibility(brls::Visibility::GONE);
    } else {
        this->subtitleTrack->setVisibility(brls::Visibility::GONE);
    }
    // 音轨选择
    if (audioOption.size() > 1) {
        int64_t value = 0;
        MPVCore::instance().get_property("aid", MPV_FORMAT_INT64, &value);
        this->audioTrack->init("main/player/audio"_i18n, audioOption, int(value - 1),
            [](int selected) { MPVCore::instance().set_property("aid", selected + 1); });
        this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    } else {
        this->audioTrack->setVisibility(brls::Visibility::GONE);
    }

/// Fullscreen
#if defined(__linux__) || defined(_WIN32)
    auto& conf = AppConfig::instance();
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
