//
// Created by fang on 2022/9/17.
//

#include "view/svg_image.hpp"
#include <borealis/core/cache_helper.hpp>
#include <fstream>
#include <sstream>

/// Icons are drawn with two fixed colors; swap them for the theme's
/// icon/default and icon/active so theme files can recolor every icon.
bool SVGImage::TINT_ICONS = false;

static void tintIcon(std::string& svg) {
    if (!SVGImage::TINT_ICONS) return;  // theme colors not registered yet
    auto theme = brls::Application::getTheme();
    auto hex = [](NVGcolor c) {
        return fmt::format("#{:02X}{:02X}{:02X}", (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
            (int)(c.b * 255.0f + 0.5f));
    };
    const std::pair<const char*, std::string> swaps[] = {
        {"#61666D", hex(theme["icon/default"])},
        {"#028087", hex(theme["icon/active"])},
    };
    for (auto& [from, to] : swaps) {
        if (to == from) continue;
        for (size_t pos = 0; (pos = svg.find(from, pos)) != std::string::npos; pos += to.size())
            svg.replace(pos, 7, to);
    }
}

SVGImage::SVGImage() {
    this->registerFilePathXMLAttribute("svg", [this](const std::string& value) { this->setImageFromSVGFile(value); });

    // 交给缓存自动处理纹理的删除
    this->setFreeTexture(false);

    // 改变窗口大小时自动更新纹理
    subscription = brls::Application::getWindowSizeChangedEvent()->subscribe([this]() {
        if (!filePath.empty()) {
            // 调整窗口大小可能发生在布局中，此时重绘会重入 invalidate() 导致崩溃，延迟到下一帧
            brls::sync([this]() {
                if (filePath.empty()) return;
                brls::Visibility v = getVisibility();
                this->setVisibility(brls::Visibility::VISIBLE);
                setImageFromSVGFile(filePath);
                this->setVisibility(v);
            });
        }
    });
}

void SVGImage::setImageFromSVGRes(const std::string& value) {
#ifdef USE_LIBROMFS
    filePath = "@res/" + value;
    if (checkCache(filePath) > 0) return;
    auto image = romfs::get(value);
    std::string svg((const char*)image.string().data(), image.size());
    tintIcon(svg);
    this->document = lunasvg::Document::loadFromData(svg);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGRes: cannot load svg image: {}", value);
        return;
    }

    size_t tex = this->getTexture();
    if (tex > 0) {
        brls::Logger::verbose("cache svg: {} {}", value, tex);
        brls::TextureCache::instance().addCache("@res/" + value, tex);
    } else {
        brls::Logger::error("svg got zero tex: {} {}", value, tex);
    }
#else
    this->setImageFromSVGFile(std::string(BRLS_RESOURCES) + value);
#endif
}

void SVGImage::setImageFromSVGFile(const std::string& value) {
    filePath = value;
#ifdef USE_LIBROMFS
    if (value.rfind("@res/", 0) == 0) return this->setImageFromSVGRes(value.substr(5));
#endif
    if (checkCache(value) > 0) return;

    std::ifstream in(value, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string svg = ss.str();
    tintIcon(svg);
    this->document = lunasvg::Document::loadFromData(svg);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGFile: cannot load svg image: {}", value);
        return;
    }

    size_t tex = this->getTexture();
    if (tex > 0) {
        brls::Logger::verbose("cache svg: {} {}", value, tex);
        brls::TextureCache::instance().addCache(value, tex);
    } else {
        brls::Logger::error("svg got zero tex: {} {}", value, tex);
    }
}

void SVGImage::setImageFromSVGString(const std::string& value) {
    this->document = lunasvg::Document::loadFromData(value);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGString: cannot load svg image: {}", value);
    }
}

void SVGImage::updateBitmap() {
    if (!this->document) return;

    float width = this->getWidth() * brls::Application::windowScale;
    float height = this->getHeight() * brls::Application::windowScale;
    auto bitmap = this->document->renderToBitmap(width, height);
    bitmap.convertToRGBA();
    NVGcontext* vg = brls::Application::getNVGContext();
    int tex = nvgCreateImageRGBA(vg, bitmap.width(), bitmap.height(), 0, bitmap.data());
    if (tex <= 0) {
        brls::Logger::error("svg: {} update bitmap with texture 0.", filePath);
        return;
    }
    this->innerSetImage(tex);
}

void SVGImage::rotate(float value) { this->angle = value; }

SVGImage::~SVGImage() { brls::Application::getWindowSizeChangedEvent()->unsubscribe(subscription); }

brls::View* SVGImage::create() { return new SVGImage(); }

void SVGImage::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    if (this->texture == 0) return;

    nvgSave(vg);
    float cx = width / 2, cy = height / 2;
    nvgTranslate(vg, x + cx, y + cy);
    nvgRotate(vg, this->angle);

    this->paint.xform[4] = -cx;
    this->paint.xform[5] = -cy;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, -cx, -cy, width, height, getCornerRadius());
    nvgFillPaint(vg, a(this->paint));
    nvgFill(vg);

    nvgRestore(vg);
}
