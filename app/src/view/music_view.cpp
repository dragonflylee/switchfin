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

    if (!playSession) {
        const auto ts = std::chrono::system_clock::now().time_since_epoch();
        playSession = std::chrono::duration_cast<std::chrono::milliseconds>(ts).count();
    }

    auto& mpv = MPVCore::instance();

    /// 播放控制
    this->btnPrev->registerClickAction([&mpv](...) {
        mpv.command("playlist-prev");
        return true;
    });
    this->btnPrev->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnPrev));

    this->btnNext->registerClickAction([&mpv](...) {
        mpv.command("playlist-next");
        return true;
    });
    this->btnNext->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnNext));

    // 获取当前播放
    int pos = mpv.getInt("playlist-playing-pos");
    if (pos > 0) {
        std::string key = fmt::format("playlist/{}/id", pos);
        auto it = playList.find(mpv.getInt(key));
        this->playTitle->setText(it->second.Name);
        this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
        this->rightStatusLabel->setText(misc::sec2Time(mpv.duration));
        this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
    }

    osdSlider->getProgressSetEvent().subscribe([](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        MPVCore::instance().seek(progress * 100, "absolute-percent");
    });

    // 注册播放事件回调
    this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::START_FILE: {
            std::string key = fmt::format("playlist/{}/id", mpv.getInt("playlist-playing-pos"));
            auto it = playList.find(mpv.getInt(key));
            this->playTitle->setText(it->second.Name);
            break;
        }
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
    // 注冊命令回調
    replySubscribeID = mpv.getCommandReply()->subscribe([](uint64_t userdata, int64_t entryId) {
        auto item = reinterpret_cast<jellyfin::MusicTrack*>(userdata);
        playList.insert(std::make_pair(entryId, *item));
    });
}

MusicView::~MusicView() {
    brls::Logger::debug("MusicView: delete");
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCommandReply()->unsubscribe(replySubscribeID);
}

brls::View* MusicView::create() { return new MusicView(); }

void MusicView::load(const std::vector<jellyfin::MusicTrack>& list) {
    auto& conf = AppConfig::instance();
    auto& mpv = MPVCore::instance();
    std::string query = HTTP::encode_form({
        {"static", "true"},
        {"PlaySessionId", std::to_string(playSession)},
    });
    std::string extra = fmt::format("http-header-fields='X-Emby-Token: {}'", conf.getUser().access_token);

    mpv.stop();
    mpv.command("playlist-clear");
    playList.clear();

    for (auto& item : list) {
        uint64_t userdata = reinterpret_cast<uint64_t>(&item);
        std::string url = fmt::format(jellyfin::apiAudio, item.Id, query);
        mpv.setUrl(conf.getUrl() + url, extra, "append", userdata);
    }
}