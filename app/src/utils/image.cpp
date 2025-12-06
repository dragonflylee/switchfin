#include "utils/image.hpp"
#include "utils/thread.hpp"
#include <fmt/format.h>
#include <borealis/core/cache_helper.hpp>
#ifdef USE_WEBP
#include <webp/decode.h>
#endif
#include <stb_image.h>

#ifdef BOREALIS_USE_GXM
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#define STB_DXT_IMPLEMENTATION
#include <borealis/extern/nanovg/stb_dxt.h>
#include <borealis/extern/nanovg/nanovg_gxm.h>

static inline __attribute__((always_inline)) uint32_t nearest_po2(uint32_t val) {
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;

    return val;
}

static inline __attribute__((always_inline)) uint64_t morton_1(uint64_t x) {
    x = x & 0x5555555555555555;
    x = (x | (x >> 1)) & 0x3333333333333333;
    x = (x | (x >> 2)) & 0x0F0F0F0F0F0F0F0F;
    x = (x | (x >> 4)) & 0x00FF00FF00FF00FF;
    x = (x | (x >> 8)) & 0x0000FFFF0000FFFF;
    x = (x | (x >> 16)) & 0xFFFFFFFFFFFFFFFF;
    return x;
}

static inline __attribute__((always_inline)) void d2xy_morton(uint64_t d, uint64_t* x, uint64_t* y) {
    *x = morton_1(d);
    *y = morton_1(d >> 1);
}

static inline __attribute__((always_inline)) void extract_block(const uint8_t* src, uint32_t width, uint8_t* block) {
    for (int j = 0; j < 4; j++) {
        memcpy(&block[j * 4 * 4], src, 16);
        src += width * 4;
    }
}

static void dxt_compress_ext(
    uint8_t* dst, uint8_t* src, uint32_t w, uint32_t h, uint32_t stride, uint32_t last_size, bool isdxt5) {
    uint8_t block[64];
    uint32_t align_w = MAX(nearest_po2(w), last_size);
    uint32_t align_h = MAX(nearest_po2(h), last_size);
    uint32_t s = MIN(align_w, align_h);
    uint32_t num_blocks = s * s / 16;
    const uint32_t block_size = isdxt5 ? 16 : 8;
    uint64_t d, offs_x, offs_y;

    for (d = 0; d < num_blocks; d++, dst += block_size) {
        d2xy_morton(d, &offs_x, &offs_y);
        if (offs_x * 4 >= h || offs_y * 4 >= w) continue;
        extract_block(src + offs_y * 16 + offs_x * stride * 16, stride, block);
        stb_compress_dxt_block(dst, block, isdxt5, STB_DXT_NORMAL);
    }
    if (align_w > align_h) return dxt_compress_ext(dst, src + s * 4, w - s, h, stride, s, isdxt5);
    if (align_w < align_h) return dxt_compress_ext(dst, src + stride * s * 4, w, h - s, stride, s, isdxt5);
}

static void dxt_compress(uint8_t* dst, uint8_t* src, uint32_t w, uint32_t h, bool isdxt5) {
    dxt_compress_ext(dst, src, w, h, w, 64, isdxt5);
}
#endif

Image::Image() : image(nullptr) {
    this->isCancel = std::make_shared<std::atomic_bool>(false);
    brls::Logger::verbose("new Image {}", fmt::ptr(this));
}

Image::~Image() { brls::Logger::verbose("delete Image {}", fmt::ptr(this)); }

void Image::with(brls::Image* view, const std::string& url) {
    int tex = brls::TextureCache::instance().getCache(url);
    if (tex > 0) {
        view->innerSetImage(tex);
        return;
    }

    Ref item;
    std::lock_guard<std::mutex> lock(requestMutex);

    if (pool.empty()) {
        item = std::make_shared<Image>();
    } else {
        item = pool.front();
        pool.pop_front();
    }

    auto it = requests.insert(std::make_pair(view, item));
    if (!it.second) {
        brls::Logger::warning("insert Image {} failed", fmt::ptr(view));
        return;
    }

    item->image = view;
    item->url = url;
    item->isCancel->store(false);
    view->ptrLock();
    // 设置图片组件不处理纹理的销毁，由缓存统一管理纹理销毁
    view->setFreeTexture(false);

    ThreadPool::instance().submit([item](HTTP& s) { item->doRequest(s); });
}

void Image::cancel(brls::Image* view) {
    brls::TextureCache::instance().removeCache(view->getTexture());
    view->clear();

    clear(view);
}

void Image::doRequest(HTTP& s) {
    if (this->isCancel->load()) {
        Image::clear(this->image);
        return;
    }
    try {
        std::ostringstream body;
        HTTP::set_option(s, this->isCancel, HTTP::Timeout{});
        s._get(url, &body);
        std::string data = body.str();
        uint8_t* imageData = nullptr;
        int imageW = 0, imageH = 0;
        bool isWebp = false;
#ifdef USE_WEBP
        char* ct = nullptr;
        if (url.find("Webp") != std::string::npos || (s.getinfo(&ct) && strcmp(ct, "image/webp") == 0)) {
            imageData = WebPDecodeRGBA((const uint8_t*)data.c_str(), data.size(), &imageW, &imageH);
            isWebp = true;
        } else
#endif
        {
            int n;
            imageData = stbi_load_from_memory((unsigned char*)data.c_str(), data.size(), &imageW, &imageH, &n, 4);
        }

#ifdef BOREALIS_USE_GXM
        if (imageData) {
            size_t size = nearest_po2(imageW) * nearest_po2(imageH);
            if (!isWebp) size >> 1;
            auto* compressed = (uint8_t*)malloc(size);
            dxt_compress(compressed, imageData, imageW, imageH, isWebp);
#ifdef USE_WEBP
            if (isWebp)
                WebPFree(imageData);
            else
#endif
                stbi_image_free(imageData);

            imageData = compressed;
        }
#endif

        brls::Logger::verbose("request Image {} size {}", this->url, data.size());
        brls::sync([this, imageData, imageW, imageH, isWebp] {
            if (!this->isCancel->load()) {
                // Load texture
                int tex = brls::TextureCache::instance().getCache(url);
                if (tex == 0 && imageData != nullptr) {
                    NVGcontext* vg = brls::Application::getNVGContext();
#ifdef BOREALIS_USE_GXM
                    tex = nvgCreateImageRGBA(
                        vg, imageW, imageH, (isWebp ? NVG_IMAGE_DXT5 : NVG_IMAGE_DXT1) | NVG_IMAGE_LPDDR, imageData);
#else
                    tex = nvgCreateImageRGBA(vg, imageW, imageH, 0, imageData);
#endif
                    brls::TextureCache::instance().addCache(this->url, tex);
                }
                if (tex > 0) this->image->innerSetImage(tex);
                clear(this->image);
            }
            if (imageData) {
#ifdef BOREALIS_USE_GXM
                free(imageData);
#else
#ifdef USE_WEBP
                if (isWebp)
                    WebPFree(imageData);
                else
#endif
                    stbi_image_free(imageData);
#endif
            }
        });
    } catch (const std::exception& ex) {
        brls::Logger::warning("request image {} {}", this->url, ex.what());
        Image::clear(this->image);
    }
}

void Image::clear(brls::Image* view) {
    std::lock_guard<std::mutex> lock(requestMutex);

    auto it = requests.find(view);
    if (it == requests.end()) return;

    it->second->image->ptrUnlock();
    it->second->image = nullptr;
    it->second->isCancel->store(true);
    pool.push_back(it->second);
    requests.erase(it);
}