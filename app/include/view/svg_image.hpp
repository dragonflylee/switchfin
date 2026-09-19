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

    void setImageFromSVGRes(const std::string& value);

    void setImageFromSVGFile(const std::string& value);

    void setImageFromSVGString(const std::string& value);

    void rotate(float value);

    void updateBitmap();

    static View* create();

private:
#ifdef PS5_NATIVE_GPU
    std::string bitmapCacheKey(const std::string& source);
    bool nativeRasterSize(std::uint32_t& width, std::uint32_t& height);
    bool useNativeCache(std::string& nextPath, const std::string& key);
    bool renderNativeDocument(const lunasvg::Document& nextDocument, const std::string* key);
#endif
    std::unique_ptr<lunasvg::Document> document = nullptr;
    brls::VoidEvent::Subscription subscription;
    std::string filePath;
    float angle = 0;
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
