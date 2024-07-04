#include "view/music_view.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/http.hpp"

using namespace brls::literals;

MusicView::MusicView() {
    this->inflateFromXMLRes("xml/view/music_view.xml");
    brls::Logger::debug("MusicView: create");

    auto& mpv = MPVCore::instance();

    /// 播放控制
    this->btnToggle->registerClickAction([&mpv](...) {
        if (mpv.isStopped())
            mpv.command("playlist-play-index", "current");
        else
            mpv.togglePlay();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

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

    this->btnSuffle->registerClickAction([this](...) { return this->toggleShuffle(); });
    this->btnSuffle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSuffle));

    this->btnRepeat->registerClickAction([this](...) { return this->toggleLoop(); });
    this->btnRepeat->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnRepeat));

    osdSlider->getProgressSetEvent().subscribe([](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        MPVCore::instance().seek(progress * 100, "absolute-percent");
    });
}

MusicView::~MusicView() { brls::Logger::debug("MusicView: delete"); }

void MusicView::registerMpvEvent() {
    auto& mpv = MPVCore::instance();
    // 生成播放 ID
    const auto ts = std::chrono::system_clock::now().time_since_epoch();
    this->playSession = std::chrono::duration_cast<std::chrono::milliseconds>(ts).count();
    // 注册播放事件回调
    this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::START_FILE:
            if (playList.size() > 0) {
                std::string key = fmt::format("playlist/{}/id", mpv.getInt("playlist-playing-pos"));
                auto it = playList.find(mpv.getInt(key));
                if (it != playList.end()) {
                    this->playTitle->setText(it->second.Name);
                    this->itemId = it->second.Id;
                    mpv.getCustomEvent()->fire(TRACK_START, &it->second);
                }
            }
            break;
        case MpvEventEnum::MPV_RESUME:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->leftStatusLabel->setText(misc::sec2Time(0));
            this->rightStatusLabel->setText(misc::sec2Time(mpv.duration));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::END_OF_FILE:
        case MpvEventEnum::MPV_STOP:
            this->reset();
            if (!this->getParent()) brls::sync([this]() { this->unregisterMpvEvent(); });
            break;
        default:;
        }
    });
    // 注冊命令回調
    replySubscribeID = mpv.getCommandReply()->subscribe([this](uint64_t userdata, int64_t entryId) {
        auto item = reinterpret_cast<jellyfin::Track*>(userdata);
        if (item) playList.insert(std::make_pair(entryId, *item));
    });

    brls::Logger::info("MusicView: registerMpvEvent {}", this->playSession);
}

void MusicView::unregisterMpvEvent() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCommandReply()->unsubscribe(replySubscribeID);

    brls::Logger::info("MusicView: unregisterMpvEvent {}", this->playSession);
    // 清空播放ID
    this->playSession = 0;
}

void MusicView::registerViewAction(brls::View* view) {
    auto& mpv = MPVCore::instance();

    view->registerAction(
        "main/player/toggle"_i18n, brls::BUTTON_Y,
        [&mpv](brls::View* view) {
            if (mpv.isStopped())
                mpv.command("playlist-play-index", "current");
            else
                mpv.togglePlay();
            return true;
        },
        true);

    view->registerAction("main/player/prev"_i18n, brls::BUTTON_LB, [&mpv](brls::View* view) {
        mpv.command("playlist-prev");
        return true;
    });

    view->registerAction("main/player/next"_i18n, brls::BUTTON_RB, [&mpv](brls::View* view) {
        mpv.command("playlist-next");
        return true;
    });
}

const std::string& MusicView::currentId() { return this->itemId; }

void MusicView::play(const jellyfin::Item& item) {
    auto& conf = AppConfig::instance();
    auto& mpv = MPVCore::instance();

    if (!this->playSession) this->registerMpvEvent();

    std::string query = HTTP::encode_form({
        {"static", "true"},
        {"PlaySessionId", std::to_string(playSession)},
    });
    std::string url = fmt::format(fmt::runtime(jellyfin::apiAudio), item.Id, query);
    std::string extra = fmt::format("http-header-fields='X-Emby-Token: {}'", conf.getUser().access_token);
    mpv.stop();
    mpv.setUrl(conf.getUrl() + url, extra);

    this->playTitle->setText(item.Name);
}

void MusicView::load(const std::vector<jellyfin::Track>& items, size_t index) {
    auto& conf = AppConfig::instance();
    auto& mpv = MPVCore::instance();
    std::string extra = fmt::format("http-header-fields='X-Emby-Token: {}'", conf.getUser().access_token);

    if (!this->playSession) this->registerMpvEvent();

    mpv.stop();
    mpv.command("playlist-clear");
    this->playList.clear();
    this->btnSuffle->setBorderThickness(0);

    std::string query = HTTP::encode_form({
        {"static", "true"},
        {"PlaySessionId", std::to_string(playSession)},
    });

    for (auto& item : items) {
        uint64_t userdata = reinterpret_cast<uint64_t>(&item);
        std::string url = fmt::format(fmt::runtime(jellyfin::apiAudio), item.Id, query);
        mpv.setUrl(conf.getUrl() + url, extra, "append", userdata);
    }

    mpv.command("playlist-play-index", std::to_string(index).c_str());
}

void MusicView::reset() {
    this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
    this->rightStatusLabel->setText("--:--");
    this->leftStatusLabel->setText("--:--");
    this->osdSlider->setProgress(0);
    this->itemId.clear();
}

bool MusicView::toggleShuffle() {
    auto& mpv = MPVCore::instance();

    if (this->btnSuffle->getBorderThickness() > 0) {
        mpv.command("playlist-unshuffle");
        this->btnSuffle->setBorderThickness(0);
    } else {
        mpv.command("playlist-shuffle");
        this->btnSuffle->setBorderThickness(2.0f);
    }
    return true;
}

bool MusicView::toggleLoop() {
    auto& mpv = MPVCore::instance();
    switch (this->repeat) {
    case RepeatNone:
        mpv.command("set", "loop-file", "inf");
        this->repeat = RepeatOne;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-repeat-song.svg");
        break;
    case RepeatOne:
        mpv.command("set", "loop-file", "no");
        mpv.command("set", "loop-playlist", "inf");
        this->repeat = RepeatAll;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-repeat-list.svg");
        break;
    default:
        mpv.command("set", "loop-playlist", "no");
        this->repeat = RepeatNone;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-playlist.svg");
    }
    return true;
}