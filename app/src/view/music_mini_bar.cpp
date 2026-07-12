#include "view/music_mini_bar.hpp"
#include "view/audio_player.hpp"
#include "view/music_now_playing.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "utils/image.hpp"

MusicMiniBar::MusicMiniBar() {
    this->inflateFromXMLRes("xml/view/music_mini_bar.xml");
    this->setVisibility(brls::Visibility::GONE);  // shown only while a queue is loaded

    // click anywhere on the bar -> open the full Now Playing over the queue
    this->registerClickAction([](brls::View*) {
        if (AudioPlayer::instance().active()) MusicNowPlaying::open();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

    this->trackSubscribeID = AudioPlayer::instance().trackEvent()->subscribe([this]() { this->refresh(); });
    this->eventSubscribeID = MPVCore::instance().getEvent()->subscribe([this](MpvEventEnum event) {
        if (!AudioPlayer::instance().active()) return;
        if (event == MpvEventEnum::MPV_RESUME)
            this->toggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
        else if (event == MpvEventEnum::MPV_PAUSE)
            this->toggleIcon->setImageFromSVGRes("icon/ico-play.svg");
    });

    this->refresh();
}

MusicMiniBar::~MusicMiniBar() {
    MPVCore::instance().getEvent()->unsubscribe(this->eventSubscribeID);
    AudioPlayer::instance().trackEvent()->unsubscribe(this->trackSubscribeID);
}

void MusicMiniBar::refresh() {
    auto& player = AudioPlayer::instance();
    if (!player.active()) {
        this->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->setVisibility(brls::Visibility::VISIBLE);

    // Seed the play/pause glyph from the live mpv state: the MPV_RESUME/MPV_PAUSE
    // events only fire on a state *change*, so when the bar first appears the icon
    // would otherwise keep the XML default (play) even while audio is playing.
    this->toggleIcon->setImageFromSVGRes(MPVCore::instance().isPaused() ? "icon/ico-play.svg" : "icon/ico-pause.svg");

    const media::Item& t = player.current();
    this->labelTitle->setText(t.title);
    std::string artist = !t.grandparentTitle.empty() ? t.grandparentTitle : t.parentTitle;
    this->labelArtist->setText(artist);

    // cancel any in-flight load first: Image::with() drops a new request while
    // one is pending for the same target, so a fast next/prev would keep a stale
    // cover (review finding). cancel() clears the request set; clear() the texture.
    Image::cancel(this->cover);
    this->cover->clear();
    std::string art = !t.thumb.empty() ? t.thumb : t.parentThumb;
    if (!art.empty()) Image::load(this->cover, art, 96);
}
