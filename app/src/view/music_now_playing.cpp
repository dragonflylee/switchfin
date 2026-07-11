#include "view/music_now_playing.hpp"
#include "view/audio_player.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"

using namespace brls::literals;  // for _i18n

void MusicNowPlaying::present(const std::vector<media::Item>& tracks, size_t index, bool shuffle) {
    if (tracks.empty()) return;
    AudioPlayer::instance().play(tracks, index, shuffle);
    MusicNowPlaying::open();
}

void MusicNowPlaying::open() { brls::Application::pushActivity(new brls::Activity(new MusicNowPlaying())); }

MusicNowPlaying::MusicNowPlaying() {
    this->inflateFromXMLRes("xml/view/music_now_playing.xml");

    // B closes the screen (audio keeps playing via the controller); without this
    // the pushed activity never popped (issue #11 T11 verification).
    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    auto& player = AudioPlayer::instance();

    this->btnToggle->registerClickAction([](...) {
        AudioPlayer::instance().toggle();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

    this->btnPrev->registerClickAction([](...) {
        AudioPlayer::instance().prev();
        return true;
    });
    this->btnPrev->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnPrev));

    this->btnNext->registerClickAction([](...) {
        AudioPlayer::instance().next();
        return true;
    });
    this->btnNext->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnNext));

    this->btnShuffle->registerClickAction([this](...) {
        auto& p = AudioPlayer::instance();
        p.setShuffle(!p.shuffled());
        this->refreshShuffle();
        return true;
    });
    this->btnShuffle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnShuffle));

    this->btnRepeat->registerClickAction([this](...) {
        AudioPlayer::instance().cycleRepeat();
        this->refreshRepeat();
        return true;
    });
    this->btnRepeat->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnRepeat));

    this->slider->getProgressSetEvent().subscribe([](float progress) {
        AudioPlayer::instance().seekPercent(progress);
    });

    // playback state from mpv
    this->eventSubscribeID = MPVCore::instance().getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->toggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->toggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->leftStatus->setText(misc::sec2Time(0));
            this->rightStatus->setText(misc::sec2Time(mpv.duration));
            if (mpv.duration > 0) this->slider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->leftStatus->setText(misc::sec2Time(mpv.video_progress));
            if (mpv.duration > 0) this->slider->setProgress(mpv.playback_time / mpv.duration);
            break;
        default:;
        }
    });

    // track changes from the controller (queue advance / skip)
    this->trackSubscribeID = player.trackEvent()->subscribe([this]() { this->refreshTrack(); });

    this->refreshTrack();
    this->refreshShuffle();
    this->refreshRepeat();
}

MusicNowPlaying::~MusicNowPlaying() {
    MPVCore::instance().getEvent()->unsubscribe(this->eventSubscribeID);
    AudioPlayer::instance().trackEvent()->unsubscribe(this->trackSubscribeID);
}

void MusicNowPlaying::refreshTrack() {
    const media::Item& t = AudioPlayer::instance().current();
    this->labelTitle->setText(t.title);
    std::string artist = !t.grandparentTitle.empty() ? t.grandparentTitle : t.parentTitle;
    this->labelArtist->setText(artist);
    // seed the play/pause glyph from the live state (MPV_RESUME/PAUSE only fire on
    // change; opening over an already-playing queue would keep the stale glyph)
    this->toggleIcon->setImageFromSVGRes(MPVCore::instance().isPaused() ? "icon/ico-play.svg" : "icon/ico-pause.svg");

    // cancel any in-flight load before reloading (Image::with drops otherwise)
    Image::cancel(this->cover);
    this->cover->clear();  // transparent -> the note placeholder behind shows
    std::string art = !t.thumb.empty() ? t.thumb : t.parentThumb;
    if (!art.empty()) Image::load(this->cover, art, 300);
}

void MusicNowPlaying::refreshShuffle() {
    this->btnShuffle->setBorderThickness(AudioPlayer::instance().shuffled() ? 2.0f : 0.0f);
}

void MusicNowPlaying::refreshRepeat() {
    switch (AudioPlayer::instance().repeatMode()) {
    case AudioPlayer::Repeat::One:
        this->repeatIcon->setImageFromSVGRes("icon/ico-repeat-song.svg");
        this->btnRepeat->setBorderThickness(2.0f);
        break;
    case AudioPlayer::Repeat::All:
        this->repeatIcon->setImageFromSVGRes("icon/ico-repeat-list.svg");
        this->btnRepeat->setBorderThickness(2.0f);
        break;
    default:
        this->repeatIcon->setImageFromSVGRes("icon/ico-playlist.svg");
        this->btnRepeat->setBorderThickness(0.0f);
    }
}
