#pragma once

#include <view/recycling_grid.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/image.hpp>

class SVGImage;

class BaseCardCell : public RecyclingGridItem {
public:
    ~BaseCardCell() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    void setWatched(bool played);

    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
    BRLS_BIND(brls::Image, picture, "video/card/picture");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelExt, "video/card/label/ext");
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

    BRLS_BIND(SVGImage, badgeFavorite, "video/card/badge/favorite");
    BRLS_BIND(brls::Label, labelRating, "video/card/label/rating");
};