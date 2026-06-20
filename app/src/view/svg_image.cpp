//
// Created by fang on 2022/9/17.
//

#include "view/svg_image.hpp"
#include <borealis/core/cache_helper.hpp>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {

/// "#RRGGBB" of the active app accent (theme token color/app). Brand icons bake
/// the legacy Plex gold; this is what we recolor them to, so they follow the
/// per-backend theme set by AppConfig::applyTheme().
std::string svgAccentHex() {
    NVGcolor c = brls::Application::getTheme().getColor("color/app");
    auto to8 = [](float f) -> int {
        int v = static_cast<int>(f * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", to8(c.r), to8(c.g), to8(c.b));
    return std::string(buf);
}

/// Replaces the legacy gold baked into the brand SVGs (the 12 *-activate sidebar
/// icons + ico-star) with `hex`, in place. Only those assets contain it, so this
/// is a no-op for every other SVG. Length-preserving (#RRGGBB == #RRGGBB).
bool recolorBakedAccent(std::string& svg, const std::string& hex) {
    static const std::string baked = "#E5A00D";
    if (hex.size() != baked.size()) return false;
    bool changed = false;
    for (size_t pos = svg.find(baked); pos != std::string::npos; pos = svg.find(baked, pos + hex.size())) {
        svg.replace(pos, baked.size(), hex);
        changed = true;
    }
    return changed;
}

}  // namespace

SVGImage::SVGImage() {
    this->registerFilePathXMLAttribute("svg", [this](const std::string& value) { this->setImageFromSVGFile(value); });

    // 交给缓存自动处理纹理的删除
    this->setFreeTexture(false);

    // 改变窗口大小时自动更新纹理
    subscription = brls::Application::getWindowSizeChangedEvent()->subscribe([this]() {
        if (!filePath.empty()) {
            brls::Visibility v = getVisibility();
            this->setVisibility(brls::Visibility::VISIBLE);
            setImageFromSVGFile(filePath);
            this->setVisibility(v);
        }
    });
}

void SVGImage::setImageFromSVGRes(const std::string& value) {
#ifdef USE_LIBROMFS
    filePath = "@res/" + value;
    // accent-keyed cache so a brand icon recolored per backend keeps a distinct
    // texture per theme; non-brand icons just gain a harmless accent suffix.
    const std::string accent = svgAccentHex();
    const std::string cacheKey = filePath + "|" + accent;
    if (checkCache(cacheKey) > 0) return;
    auto image = romfs::get(value);
    std::string data(reinterpret_cast<const char*>(image.string().data()), image.size());
    recolorBakedAccent(data, accent);
    this->document = lunasvg::Document::loadFromData(data);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGRes: cannot load svg image: {}", value);
        return;
    }

    size_t tex = this->getTexture();
    if (tex > 0) {
        brls::Logger::verbose("cache svg: {} {}", value, tex);
        brls::TextureCache::instance().addCache(cacheKey, tex);
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
    const std::string accent = svgAccentHex();
    const std::string cacheKey = value + "|" + accent;
    if (checkCache(cacheKey) > 0) return;

    std::ifstream in(value, std::ios::binary);
    if (!in) {
        brls::Logger::error("setImageFromSVGFile: cannot open svg image: {}", value);
        return;
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    recolorBakedAccent(data, accent);
    this->document = lunasvg::Document::loadFromData(data);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGFile: cannot load svg image: {}", value);
        return;
    }

    size_t tex = this->getTexture();
    if (tex > 0) {
        brls::Logger::verbose("cache svg: {} {}", value, tex);
        brls::TextureCache::instance().addCache(cacheKey, tex);
    } else {
        brls::Logger::error("svg got zero tex: {} {}", value, tex);
    }
}

void SVGImage::setImageFromSVGString(const std::string& value) {
    std::string data = value;
    recolorBakedAccent(data, svgAccentHex());
    this->document = lunasvg::Document::loadFromData(data);
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
