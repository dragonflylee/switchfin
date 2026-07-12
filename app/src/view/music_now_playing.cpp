#include "view/music_now_playing.hpp"
#include "view/audio_player.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "view/recycling_grid.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include <algorithm>

using namespace brls::literals;  // for _i18n

namespace {

/// Queue pane rows + data source. The queue is visible, selectable (A jumps to
/// the track) and reorderable (Y grabs a row, then D-pad up/down move it — the
/// LibraryManager grab idiom, adapted to the recycling grid).
class QueueDataSource;

class QueueCell : public RecyclingGridItem {
public:
    QueueCell();  // defined below (its actions call into QueueDataSource)
    static RecyclingGridItem* create() { return new QueueCell(); }

    void setTrack(const media::Item& t, bool nowPlaying, bool grabbed) {
        this->imgCover->clear();  // transparent -> note placeholder behind shows
        std::string art = !t.thumb.empty() ? t.thumb : t.parentThumb;
        if (!art.empty()) Image::load(this->imgCover, art, 88);
        this->labelTitle->setText(t.title);
        std::string who = !t.grandparentTitle.empty() ? t.grandparentTitle : t.parentTitle;
        this->labelArtist->setText(who);
        this->labelArtist->setVisibility(who.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        this->labelDur->setText(t.duration > 0 ? misc::sec2Time(t.duration / 1000) : "");
        this->setNowPlaying(nowPlaying);
        this->setGrabbed(grabbed);
    }

    void setNowPlaying(bool on) {
        this->iconStatus->setVisibility(on ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->labelTitle->setTextColor(brls::Application::getTheme().getColor(on ? "color/app" : "brls/text"));
    }

    void setGrabbed(bool on) {
        this->setBorderThickness(on ? 3.0f : 0.0f);
        if (on) this->setBorderColor(brls::Application::getTheme().getColor("color/app"));
    }

    void prepareForReuse() override { this->imgCover->clear(); }
    void cacheForReuse() override { Image::cancel(this->imgCover); }

    QueueDataSource* ds = nullptr;

private:
    BRLS_BIND(brls::Image, imgCover, "queue/row/cover");
    BRLS_BIND(brls::Label, labelTitle, "queue/row/title");
    BRLS_BIND(brls::Label, labelArtist, "queue/row/artist");
    BRLS_BIND(SVGImage, iconStatus, "queue/row/status");
    BRLS_BIND(brls::Label, labelDur, "queue/row/dur");
};

class QueueDataSource : public RecyclingGridDataSource {
public:
    static constexpr float ROW_HEIGHT = 56;  // 44 cover + 2x6 padding

    explicit QueueDataSource(RecyclingGrid* grid) : grid(grid) {}

    size_t getItemCount() override { return AudioPlayer::instance().queueCount(); }

    float heightForRow(brls::View*, size_t) override { return ROW_HEIGHT; }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<QueueCell*>(recycler->dequeueReusableCell("Cell"));
        auto& p = AudioPlayer::instance();
        cell->ds = this;
        cell->setTrack(p.queueItemAt(index), (int)index == p.queuePos(), (int)index == this->grabbedIndex);
        return cell;
    }

    void onItemSelected(brls::Box*, size_t index) override {
        if (this->grabbedIndex >= 0) return this->drop();  // A drops a grabbed row
        AudioPlayer::instance().playAt(index);
    }

    void clearData() override {}

    bool isGrabbed(int index) const { return this->grabbedIndex == index; }

    void toggleGrab(int index) {
        this->grabbedIndex = (this->grabbedIndex == index) ? -1 : index;
        this->refocus(index);
    }

    void drop() {
        if (this->grabbedIndex < 0) return;
        int i = this->grabbedIndex;
        this->grabbedIndex = -1;
        this->refocus(i);
    }

    void moveGrabbed(int delta) {
        if (this->grabbedIndex < 0) return;
        int j = this->grabbedIndex + delta;
        if (j < 0 || j >= (int)AudioPlayer::instance().queueCount()) return;
        AudioPlayer::instance().moveInQueue(this->grabbedIndex, j);
        this->grabbedIndex = j;
        this->refocus(j);
    }

private:
    // rebuild visible rows (grab / now-playing visuals) and keep focus on `index`:
    // the previously focused cell was requeued by reloadData, so re-hand focus.
    void refocus(int index) {
        this->grid->setDefaultCellFocus(index);
        this->grid->reloadData();
        brls::Application::giveFocus(this->grid);
    }

    RecyclingGrid* grid;
    int grabbedIndex = -1;
};

QueueCell::QueueCell() {
    this->inflateFromXMLRes("xml/view/queue_row.xml");
    this->setHighlightCornerRadius(12);

    this->registerAction("main/music/move"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        if (this->ds) this->ds->toggleGrab((int)this->getIndex());
        return true;
    });
    this->registerAction(
        "", brls::BUTTON_NAV_UP,
        [this](brls::View*) {
            if (this->ds && this->ds->isGrabbed((int)this->getIndex())) {
                this->ds->moveGrabbed(-1);
                return true;
            }
            return false;  // not grabbed: normal navigation
        },
        true, true);
    this->registerAction(
        "", brls::BUTTON_NAV_DOWN,
        [this](brls::View*) {
            if (this->ds && this->ds->isGrabbed((int)this->getIndex())) {
                this->ds->moveGrabbed(1);
                return true;
            }
            return false;
        },
        true, true);
}

}  // namespace

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

    // D-pad LEFT/RIGHT on the focused progress bar seeks the track (steps scale
    // with length, repeat when held); mirrors the video OSD idiom. The slider is
    // focusable in the XML, so its inner pointer takes focus (getDefaultFocus)
    // and these actions bubble up to it. UP/DOWN move between the bar and the
    // transport row.
    this->slider->registerAction(
        "", brls::BUTTON_NAV_LEFT,
        [](brls::View*) {
            AudioPlayer::instance().seekStep(false);
            return true;
        },
        true, true);
    this->slider->registerAction(
        "", brls::BUTTON_NAV_RIGHT,
        [](brls::View*) {
            AudioPlayer::instance().seekStep(true);
            return true;
        },
        true, true);
    this->slider->getDefaultFocus()->setCustomNavigationRoute(brls::FocusDirection::DOWN, "musicnp/toggle");
    this->btnShuffle->setCustomNavigationRoute(brls::FocusDirection::UP, "musicnp/progress");
    this->btnPrev->setCustomNavigationRoute(brls::FocusDirection::UP, "musicnp/progress");
    this->btnToggle->setCustomNavigationRoute(brls::FocusDirection::UP, "musicnp/progress");
    this->btnNext->setCustomNavigationRoute(brls::FocusDirection::UP, "musicnp/progress");
    this->btnRepeat->setCustomNavigationRoute(brls::FocusDirection::UP, "musicnp/progress");

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

    // queue pane: visible, selectable (A jumps), reorderable (Y grab + up/down)
    this->queueList->registerCell("Cell", []() { return QueueCell::create(); });
    this->queueList->setDataSource(new QueueDataSource(this->queueList));

    // track changes from the controller (queue advance / skip): update the
    // player + move the now-playing marker in the queue (no full reload)
    this->trackSubscribeID = player.trackEvent()->subscribe([this]() {
        this->refreshTrack();
        this->refreshQueueHighlight();
    });
    // queue structure changes (new queue, shuffle, reorder): rebuild the pane
    this->queueSubscribeID = player.queueEvent()->subscribe([this]() { this->rebuildQueue(); });

    this->refreshTrack();
    this->refreshShuffle();
    this->refreshRepeat();
    this->rebuildQueue();
}

MusicNowPlaying::~MusicNowPlaying() {
    MPVCore::instance().getEvent()->unsubscribe(this->eventSubscribeID);
    AudioPlayer::instance().trackEvent()->unsubscribe(this->trackSubscribeID);
    AudioPlayer::instance().queueEvent()->unsubscribe(this->queueSubscribeID);
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

void MusicNowPlaying::rebuildQueue() {
    // land on the now-playing track when navigating into the pane
    this->queueList->setDefaultCellFocus((size_t)std::max(0, AudioPlayer::instance().queuePos()));
    this->queueList->reloadData();  // no-op before first layout; onLayout reloads
}

void MusicNowPlaying::refreshQueueHighlight() {
    // move the now-playing marker on the visible rows without a reload (which
    // would reset scroll / focus while the user browses the queue)
    int pos = AudioPlayer::instance().queuePos();
    for (auto* item : this->queueList->getGridItems()) {
        auto* cell = dynamic_cast<QueueCell*>(item);
        if (cell) cell->setNowPlaying((int)cell->getIndex() == pos);
    }
}
