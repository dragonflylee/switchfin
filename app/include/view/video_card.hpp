#pragma once

#include <view/recycling_grid.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/image.hpp>

class SVGImage;

class BaseVideoCard : public RecyclingGridItem {
public:
    ~BaseVideoCard() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    BRLS_BIND(brls::Image, picture, "video/card/picture");
};

class VideoCardCell : public BaseVideoCard {
public:
    VideoCardCell() { this->inflateFromXMLRes("xml/view/video_card.xml"); }

    static VideoCardCell* create() { return new VideoCardCell(); }

    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelExt, "video/card/label/ext");
    BRLS_BIND(brls::Label, labelRating, "video/card/label/rating");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};