#pragma once

#include <borealis.hpp>
#include "view/recycling_grid.hpp"
#include "utils/image.hpp"

class VideoCardCell : public RecyclingGridItem {
public:
    VideoCardCell() { this->inflateFromXMLRes("xml/view/video_card.xml"); }

    ~VideoCardCell() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    static VideoCardCell* create() { return new VideoCardCell(); }

    BRLS_BIND(brls::Image, picture, "video/card/picture");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelYear, "video/card/label/year");
    BRLS_BIND(brls::Label, labelDuration, "video/card/label/duration");
};