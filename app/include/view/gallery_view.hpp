//
// Created by fang on 2022/8/21.
//

#pragma once

#include <borealis.hpp>

typedef std::pair<std::string, std::string> GalleryItemData;
typedef std::vector<GalleryItemData> GalleryData;

enum class GalleryAnimation { ENTER_LEFT, ENTER_RIGHT, EXIT_LEFT, EXIT_RIGHT };

class GalleryItem : public brls::Box {
public:
    GalleryItem();

    ~GalleryItem() override = default;

    void animate(GalleryAnimation animation);

    void startScrolling(float newScroll);

private:
    brls::Animatable contentOffsetX = 0.0f;
};

class ImageGalleryItem : public GalleryItem {
public:
    ImageGalleryItem();

    virtual void setData(GalleryItemData value);

private:
    BRLS_BIND(brls::Image, image, "gallery/image");
    BRLS_BIND(brls::Label, label, "gallery/label");
    GalleryItemData data;
};

class GalleryView : public brls::Box {
public:
    GalleryView();

    ~GalleryView() override;

    static View *create();

    void setData(GalleryData value);

    void addCustomView(GalleryItem *view);

    // Show the item at index immediately.
    void setIndex(unsigned int index);

    unsigned int getIndex() { return this->index; }

    unsigned int getItemCount() { return this->getChildren().size(); }

    void prev();

    void next();

    void setIndicatorPosition(float height);

    void draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext *ctx) override;

private:
    GalleryData data;
    unsigned int index = 0;
    float indicatorPosition = 0.98;
};