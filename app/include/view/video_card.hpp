#pragma once

#include <view/recycling_grid.hpp>
#include <view/long_press_gesture.hpp>
#include <utils/image.hpp>

class SVGImage;

class BaseCardCell : public RecyclingGridItem {
public:
    BaseCardCell() {
        // long press (mouse/touch) = open the context menu: replays the
        // cell's BUTTON_X action (the same as F4 / KeyBind Setting),
        // after focusing the cell. Single point: covers home, grids,
        // search, show episodes and person page.
        this->addGestureRecognizer(new LongPressGestureRecognizer(this));
    }

    ~BaseCardCell() { Image::cancel(this->picture); }

    void prepareForReuse() override {
        this->picture->setImageFromRes("img/video-card-bg.png");
        // recycled cell: never a ticker inherited from a previous bind
        this->setLabelsTicker(false);
        // nor a stale "play on select" overlay
        this->playOverlay = false;
        if (auto* ov = this->getView("video/card/play_overlay")) ov->setVisibility(brls::Visibility::GONE);
    }

    /// Enables the focus "play" overlay — only for cards that START playback on
    /// select (continue-watching, episodes, clips). Opening a detail page does
    /// not get it. The orange wash colour is applied here (theme accent, low
    /// alpha); the white disc + orange glyph live in video_card.xml.
    void setPlayOverlay(bool on) {
        this->playOverlay = on;
        if (auto* ov = this->getView("video/card/play_overlay")) {
            ov->setBackgroundColor(nvgRGBA(229, 160, 13, 64));
            if (!on) ov->setVisibility(brls::Visibility::GONE);
        }
    }

    void cacheForReuse() override { Image::cancel(this->picture); }

    /// ---- title/subtitle ticker driven by the card's focus ----
    /// Focus lives on pic_box (getDefaultFocus), not on the cell root:
    /// so we listen to the onChildFocus* bubbling rather than
    /// onFocusGained/Lost. setAnimated(false) restores the ellipsis (onLayout).
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override {
        this->setLabelsTicker(true);
        if (this->playOverlay)
            if (auto* ov = this->getView("video/card/play_overlay")) ov->setVisibility(brls::Visibility::VISIBLE);
        RecyclingGridItem::onChildFocusGained(directChild, focusedView);
    }

    void onChildFocusLost(brls::View* directChild, brls::View* focusedView) override {
        this->setLabelsTicker(false);
        if (auto* ov = this->getView("video/card/play_overlay")) ov->setVisibility(brls::Visibility::GONE);
        RecyclingGridItem::onChildFocusLost(directChild, focusedView);
    }

    /// Box::onParentFocus* broadcasts the event to the WHOLE descendance: a
    /// focused ancestor (tab, grid) lit the ticker of every card via
    /// Label::autoAnimate — hence labels scrolling permanently.
    /// Swallowed here: scrolling now only depends on the focus of the
    /// card itself (onChildFocus* hooks above).
    void onParentFocusGained(brls::View* focusedView) override { (void)focusedView; }

    void onParentFocusLost(brls::View* focusedView) override { (void)focusedView; }

    /// the focus halo only surrounds the poster, not the title block.
    /// Borealis contract: getDefaultFocus may return this/nullptr but must
    /// NEVER throw — resolution by id (nullptr if absent from the
    /// subclass layout) instead of a BRLS_BIND binding that throws
    /// (ViewNotFoundException -> SIGABRT).
    brls::View* getDefaultFocus() override {
        brls::View* pic = this->getView("video/card/pic_box");
        if (pic && pic->isFocusable()) return pic;
        return RecyclingGridItem::getDefaultFocus();
    }

    BRLS_BIND(brls::Image, picture, "video/card/picture");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelExt, "video/card/label/ext");

private:
    /// Enables/disables the ticker of the card's singleLine labels.
    /// Resolution by id with silent fallback (same anti-throw contract as
    /// getDefaultFocus): subclasses do not all have the same labels
    /// (episode_card.xml has no video/card/label/*, and vice versa).
    void setLabelsTicker(bool on) {
        static const char* ids[] = {"video/card/label/title", "video/card/label/ext", "episode/card/name"};
        for (auto* id : ids) {
            if (auto* label = dynamic_cast<brls::Label*>(this->getView(id))) label->setAnimated(on);
        }
    }

    bool playOverlay = false;  // show the focus "play" overlay (set per item type)
};

class MediaCardCell : public BaseCardCell {
public:
    MediaCardCell() { this->inflateFromXMLRes("xml/view/video_card.xml"); }

    static MediaCardCell* create() { return new MediaCardCell(); }
};

class VideoCardCell : public BaseCardCell {
public:
    VideoCardCell();

    static VideoCardCell* create() { return new VideoCardCell(); }

    /// In-place "watched" badge refresh — targeted update from a context-menu
    /// scrobble, without reloading the list.
    void setWatched(bool played);

    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};