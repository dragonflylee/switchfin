//
// Created by fang on 2022/9/17.
//

#include "view/svg_image.hpp"
#include <borealis/core/cache_helper.hpp>

#ifdef PS5_NATIVE_GPU
#include <cmath>
#include <limits>

namespace {
// A failed cache insertion leaves the new texture with its creator. Release
// this guard only when Image or TextureCache consumes that ownership.
struct NativeSvgTexture {
    NVGcontext* context;
    int id;
    ~NativeSvgTexture() { if (id > 0) nvgDeleteImage(context, id); }
    int release() { const int result = id; id = 0; return result; }
};
}

#endif
SVGImage::SVGImage() {
    this->registerFilePathXMLAttribute("svg", [this](const std::string& value) { this->setImageFromSVGFile(value); });

    // 交给缓存自动处理纹理的删除
#ifdef PS5_NATIVE_GPU
#else
    this->setFreeTexture(false);
#endif

    // 改变窗口大小时自动更新纹理
    subscription = brls::Application::getWindowSizeChangedEvent()->subscribe([this]() {
        if (!filePath.empty()) {
            brls::Visibility v = getVisibility();
            this->setVisibility(brls::Visibility::VISIBLE);
            setImageFromSVGFile(filePath);
            this->setVisibility(v);
        }
#ifdef PS5_NATIVE_GPU
        else if (document) {
            updateBitmap();
        }
#endif
    });
#ifdef PS5_NATIVE_GPU
}

std::string SVGImage::bitmapCacheKey(const std::string& source) {
    // One SVG can appear at different sizes, and a render-resolution change
    // needs a new raster. A path-only hit reused the old low-resolution bitmap
    // before the resize callback could regenerate it.
    std::uint32_t width = 0, height = 0;
    if (!nativeRasterSize(width, height)) return {};
    return fmt::format("@svg:{}:{}x{}", source, width, height);
}

bool SVGImage::nativeRasterSize(std::uint32_t& width, std::uint32_t& height) {
    const double w = static_cast<double>(getWidth()) * brls::Application::windowScale;
    const double h = static_cast<double>(getHeight()) * brls::Application::windowScale;
    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 ||
        w > std::numeric_limits<int>::max() || h > std::numeric_limits<int>::max()) return false;
    const auto nextWidth = static_cast<std::uint32_t>(w);
    const auto nextHeight = static_cast<std::uint32_t>(h);
    // The pinned lunasvg Bitmap::Impl evaluates width*height*4 in uint32_t;
    // Canvas then passes its stride through int to plutovg. Validate those
    // actual arithmetic limits, without imposing an unrelated artwork budget.
    if (nextWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4)) return false;
    const std::uint32_t row = nextWidth * 4;
    if (nextHeight > std::numeric_limits<std::uint32_t>::max() / row ||
        nextHeight > std::numeric_limits<std::size_t>::max() / row) return false;
    width = nextWidth;
    height = nextHeight;
    return true;
}

bool SVGImage::useNativeCache(std::string& nextPath, const std::string& key) {
    auto& cache = brls::TextureCache::instance();
    if (cache.isClosing()) return true;
    const int tex = cache.getCache(key);
    if (tex <= 0) return false;
    this->setImageFromCache(tex); // Consumes the acquired cache reference, even on rejection.
    if (this->getTexture() == tex) {
        // A bitmap hit has no associated parsed document. Retain a document
        // only when it already belongs to this same source.
        if (filePath != nextPath) document.reset();
        filePath.swap(nextPath);
    }
    return true;
}

bool SVGImage::renderNativeDocument(const lunasvg::Document& nextDocument, const std::string* key) {
    auto& cache = brls::TextureCache::instance();
    std::uint32_t width = 0, height = 0;
    if (cache.isClosing() || !nativeRasterSize(width, height) ||
        !std::isfinite(nextDocument.width()) || !std::isfinite(nextDocument.height()) ||
        nextDocument.width() <= 0 || nextDocument.height() <= 0) return false;
    auto bitmap = nextDocument.renderToBitmap(width, height);
    if (!bitmap.valid() || !bitmap.data() || bitmap.width() != width || bitmap.height() != height ||
        bitmap.stride() != width * 4 || cache.isClosing()) return false;
    bitmap.convertToRGBA();
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg || cache.isClosing()) return false;
    NativeSvgTexture next{vg, nvgCreateImageRGBA(vg, width, height, 0, bitmap.data())};
    if (next.id <= 0 || cache.isClosing()) return false;
    const int tex = next.id;
    if (key) {
        if (!cache.tryAddCache(*key, tex)) return false;
        next.release(); // The cache now owns the texture and one acquired reference.
        this->setImageFromCache(tex);
    } else {
        const bool previousPolicy = this->getFreeTexture();
        this->setFreeTexture(true);
        this->innerSetImage(next.release()); // Owned memory/string texture, including rejected input.
        this->setFreeTexture(previousPolicy);
    }
    return this->getTexture() == tex;
#endif
}

void SVGImage::setImageFromSVGRes(const std::string& value) {
#ifdef USE_LIBROMFS
#ifdef PS5_NATIVE_GPU
    if (brls::TextureCache::instance().isClosing()) return;
    try {
        std::string nextPath = "@res/" + value;
        const std::string key = bitmapCacheKey(nextPath);
        if (key.empty() || useNativeCache(nextPath, key)) return;
        const auto image = romfs::get(value);
        const auto data = image.string();
        auto nextDocument = lunasvg::Document::loadFromData(data.data(), data.size());
        if (!nextDocument || !renderNativeDocument(*nextDocument, &key)) {
            brls::Logger::error("svg: resource replacement failed");
            return;
        }
        document = std::move(nextDocument);
        filePath.swap(nextPath);
    } catch (const std::exception&) {
        brls::Logger::error("svg: resource replacement failed");
#else
    filePath = "@res/" + value;
    if (checkCache(filePath) > 0) return;
    auto image = romfs::get(value);
    this->document = lunasvg::Document::loadFromData((const char*)image.string().data(), image.size());
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
#endif
    }
#else
#ifdef PS5_NATIVE_GPU
    if (brls::TextureCache::instance().isClosing()) return;
    try {
        this->setImageFromSVGFile(brls::resourceBase() + value);
    } catch (const std::exception&) {
        brls::Logger::error("svg: resource replacement failed");
    }
#else
    this->setImageFromSVGFile(std::string(BRLS_RESOURCES) + value);
#endif
#endif
}

void SVGImage::setImageFromSVGFile(const std::string& value) {
#ifdef PS5_NATIVE_GPU
    if (brls::TextureCache::instance().isClosing()) return;
    try {
#else
    filePath = value;
#endif
#ifdef USE_LIBROMFS
#ifdef PS5_NATIVE_GPU
        if (value.rfind("@res/", 0) == 0) return this->setImageFromSVGRes(value.substr(5));
#else
    if (value.rfind("@res/", 0) == 0) return this->setImageFromSVGRes(value.substr(5));
#endif
#endif
#ifdef PS5_NATIVE_GPU
        std::string nextPath = value;
        const std::string key = bitmapCacheKey(nextPath);
        if (key.empty() || useNativeCache(nextPath, key)) return;
        auto nextDocument = lunasvg::Document::loadFromFile(nextPath);
        if (!nextDocument || !renderNativeDocument(*nextDocument, &key)) {
            brls::Logger::error("svg: file replacement failed");
            return;
        }
        document = std::move(nextDocument);
        filePath.swap(nextPath);
    } catch (const std::exception&) {
        brls::Logger::error("svg: file replacement failed");
#else
    if (checkCache(value) > 0) return;

    this->document = lunasvg::Document::loadFromFile(value);
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
#endif
    }
}

void SVGImage::setImageFromSVGString(const std::string& value) {
#ifdef PS5_NATIVE_GPU
    if (brls::TextureCache::instance().isClosing()) return;
    try {
        auto nextDocument = lunasvg::Document::loadFromData(value);
        if (!nextDocument || !renderNativeDocument(*nextDocument, nullptr)) {
            brls::Logger::error("svg: string replacement failed");
            return;
        }
        document = std::move(nextDocument);
        filePath.clear();
    } catch (const std::exception&) {
        brls::Logger::error("svg: string replacement failed");
#else
    this->document = lunasvg::Document::loadFromData(value);
    if (this->document) {
        this->updateBitmap();
    } else {
        brls::Logger::error("setImageFromSVGString: cannot load svg image: {}", value);
#endif
    }
}

void SVGImage::updateBitmap() {
#ifdef PS5_NATIVE_GPU
    if (!document || brls::TextureCache::instance().isClosing()) return;
    try {
        std::string key;
        if (!filePath.empty()) {
            key = bitmapCacheKey(filePath);
            if (key.empty() || useNativeCache(filePath, key)) return;
        }
        if (!renderNativeDocument(*document, filePath.empty() ? nullptr : &key))
            brls::Logger::error("svg: bitmap replacement failed");
    } catch (const std::exception&) {
        brls::Logger::error("svg: bitmap replacement failed");
#else
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
#endif
    }
#ifdef PS5_NATIVE_GPU
#else
    this->innerSetImage(tex);
#endif
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
