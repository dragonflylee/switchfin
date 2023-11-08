#include "view/music_view.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/http.hpp"

MusicView::MusicView() {
    this->inflateFromXMLRes("xml/view/music_view.xml");
    brls::Logger::debug("MusicView: create");

    auto& mpv = MPVCore::instance();

    /// 播放控制
    this->btnPrev->registerClickAction([&mpv](...) {
        mpv.command_str("playlist-prev");
        return true;
    });
    this->btnPrev->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnPrev));

    this->btnNext->registerClickAction([&mpv](...) {
        mpv.command_str("playlist-next");
        return true;
    });
    this->btnNext->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnNext));

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        MPVCore::instance().command_str("seek {} absolute-percent", progress * 100);
    });

    this->eventSubscribeID = mpv.getEvent().subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->rightStatusLabel->setText(misc::sec2Time(mpv.duration));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::END_OF_FILE:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        default:;
        }
    });
}

MusicView::~MusicView() {
    brls::Logger::debug("MusicView: delete");
    auto& ev = MPVCore::instance().getEvent();
    ev.unsubscribe(eventSubscribeID);
}

brls::View* MusicView::create() { return new MusicView(); }

void MusicView::load(const std::vector<jellyfin::MusicTrack>& list) {
    auto& conf = AppConfig::instance();
    const auto ts = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ts);

    auto& mpv = MPVCore::instance();
    std::string query = HTTP::encode_form({
        {"static", "true"},
        {"PlaySessionId", std::to_string(ms.count())},
    });
    std::string extra = fmt::format("http-header-fields='X-Emby-Token: {}'", conf.getUser().access_token);

    mpv.command_str("playlist-clear");
    for (auto& item : list) {
        mpv.setUrl(fmt::format("{}/Audio/{}/stream?{}", conf.getUrl(), item.Id, query), extra, "append");
    }
}