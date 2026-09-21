#pragma once

#include <view/recycling_grid.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/image.hpp>

class SVGImage;

class BaseCardCell : public RecyclingGridItem {
public:
#ifdef PS5_NATIVE_GPU
    ~BaseCardCell() {
        this->picture->setArtworkRetryHandler(nullptr, nullptr);
        Image::cancel(this->picture);
    }
#else
    ~BaseCardCell() { Image::cancel(this->picture); }
#endif

#ifdef PS5_NATIVE_GPU
    void prepareForReuse() override;
#else
    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }
#endif

    void cacheForReuse() override { Image::cancel(this->picture); }

    void setWatched(bool played);

    void setFavorite(bool favorite);

    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(SVGImage, badgeFavorite, "video/card/badge/favorite");
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

    BRLS_BIND(brls::Label, labelRating, "video/card/label/rating");
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
