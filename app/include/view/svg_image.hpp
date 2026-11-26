//
// Created by fang on 2022/6/5.
//

#pragma once

#include <borealis.hpp>
#include <lunasvg.h>

class SVGImage : public brls::Image {
public:
    SVGImage();

    ~SVGImage() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    void setImageFromSVGRes(const std::string& value, const std::string& tint = "icon/default");

    /// `tint` overrides the icon/default theme color used for the primary glyph
    void setImageFromSVGFile(const std::string& value, const std::string& tint = "icon/default");

    void setImageFromSVGString(const std::string& value);

    void rotate(float value);

    void updateBitmap();

    static View* create();

private:
    std::unique_ptr<lunasvg::Document> document = nullptr;
    brls::VoidEvent::Subscription subscription;
    std::string filePath;
    std::string tint = "icon/default";
    float angle = 0;

    void loadSVG(const std::string& value, std::string svg, const std::string& tint);
};