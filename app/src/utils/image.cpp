#include "utils/image.hpp"
#include "utils/thread.hpp"
#include <fmt/format.h>
#include <borealis/core/cache_helper.hpp>
#ifdef USE_WEBP
#include <webp/decode.h>
#endif
#include <stb_image.h>
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_artwork_flights.hpp"
#include "utils/ps5_native_artwork_body.hpp"
#include "utils/ps5_native_artwork_pixels.hpp"
#include "utils/ps5_native_artwork_scheduler.hpp"
#include <climits>

namespace {
using NativeArtworkBody = ps5::artwork::EncodedBody<>;
struct NativeArtworkPixels {
    std::string sourceUrl;
    HTTP::Header headers;
    std::shared_ptr<unsigned char> data;
    std::shared_ptr<NativeArtworkBody> encoded;
    size_t pixelBytes = 0;
    bool webp = false;
    bool pixelPressure = false;
    int width = 0;
    int height = 0;
};
using NativeArtworkRequests = ps5::artwork::Flights<brls::Image, NativeArtworkPixels>;
using NativeArtworkScheduler = ps5::artwork::Scheduler<NativeArtworkRequests::Ref>;
void pumpNativeArtwork(bool allowDelivery,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
void retireNativeArtwork(const NativeArtworkRequests::Ref& request) noexcept;

NativeArtworkScheduler& nativeArtworkScheduler() {
    static NativeArtworkScheduler scheduler;
    return scheduler;
}

NativeArtworkRequests& nativeArtworkRequests() {
    static NativeArtworkRequests requests;
    return requests;
}

ps5::artwork::EncodedBudget& nativeArtworkEncodedBudget() {
    static ps5::artwork::EncodedBudget budget;
    return budget;
}

ps5::artwork::EncodedBudget& nativeArtworkPixelBudget() {
    static ps5::artwork::EncodedBudget budget(ps5::artwork::pixelResidentLimit);
    return budget;
}

void reportNativeArtworkFailure() noexcept {
    // UI thread only, fixed message and bounded reporting even under OOM.
    static unsigned reported = 0;
    if (reported == 16) return;
    ++reported;
    try { brls::Logger::warning("Native artwork request failed"); } catch (...) {}
}

void prepareNativeArtwork() {
    static const bool registered = [] {
        // Register both handlers as one UI-owned transaction. A failed pump
        // subscription must not accumulate exit handlers on later retries.
        auto* exit = brls::Application::getExitEvent();
        const auto subscription = exit->subscribe([] {
            nativeArtworkRequests().close();
            nativeArtworkScheduler().close(retireNativeArtwork);
        });
        try {
            brls::Application::getRunLoopEvent()->subscribe([] { pumpNativeArtwork(true); });
        } catch (...) {
            exit->unsubscribe(subscription);
            throw;
        }
        return true;
    }();
    (void)registered;
}

// Credentials affect response identity. Keep this in-memory key out of logs;
// lengths distinguish header boundaries and preserve ordered/repeated headers.
std::string nativeArtworkKey(const std::string& url, const HTTP::Header& headers) {
    constexpr size_t limit = ps5::artwork::DemandLimits::keyBytes;
    if (url.empty() || url.size() > limit || headers.size() > ps5::artwork::DemandLimits::headers) return {};
    if (headers.empty()) return url;
    if (url.size() == limit) return {};
    size_t length = url.size() + 1;
    for (const auto& header : headers) {
        size_t digits = 1;
        for (size_t value = header.size(); value >= 10; value /= 10) ++digits;
        if (header.size() > limit - length || digits + 1 > limit - length - header.size()) return {};
        length += header.size() + digits + 1;
    }
    std::string key;
    key.reserve(length);
    key += url;
    key.push_back('\0');
    for (const auto& header : headers) {
        key += std::to_string(header.size()) + ":" + header;
    }
    return key;
}

void downloadNativeArtwork(const NativeArtworkRequests::Ref& request, HTTP& http) {
    if (request->cancelled->load()) return;
    auto& payload = request->payload;
    payload.pixelPressure = false;
    struct ReleaseBody {
        NativeArtworkPixels& payload;
        ~ReleaseBody() { if (!payload.pixelPressure) payload.encoded.reset(); }
    } releaseBody{payload};
    if (!payload.encoded) {
        auto data = std::make_shared<NativeArtworkBody>(nativeArtworkEncodedBudget());
        std::ostream body(data.get());
        HTTP::set_option(http, request->cancelled, HTTP::Timeout{});
        const auto& url = payload.sourceUrl.empty() ? request->url : payload.sourceUrl;
        HTTP::set_option(http, payload.headers);
        http._get(url, &body);
        if (!body.good() || request->cancelled->load() || data->empty() || data->size() > INT_MAX) return;
        int width = 0, height = 0;
        bool webp = false, sixteenBit = false;
#ifdef USE_WEBP
        char* contentType = nullptr;
        if (url.find("Webp") != std::string::npos ||
            (http.getinfo(&contentType) && contentType && strcmp(contentType, "image/webp") == 0)) {
            webp = true;
            if (!WebPGetInfo(reinterpret_cast<const uint8_t*>(data->data()), data->size(), &width, &height)) return;
        } else
#endif
        {
            int channels = 0;
            if (!stbi_info_from_memory(reinterpret_cast<const unsigned char*>(data->data()),
                    static_cast<int>(data->size()), &width, &height, &channels)) return;
            sixteenBit = stbi_is_16_bit_from_memory(reinterpret_cast<const unsigned char*>(data->data()),
                static_cast<int>(data->size())) != 0;
        }
        const auto bytes = ps5::artwork::admittedPixelBytes(width, height, sixteenBit);
        if (!bytes) return;
        payload.encoded = std::move(data);
        payload.width = width;
        payload.height = height;
        payload.pixelBytes = bytes;
        payload.webp = webp;
    }
    // Only an explicit resident-pixel reservation refusal retries. The same
    // already-budgeted body survives backoff; HTTP and malformed/decode failures
    // are terminal, and no worker blocks waiting for another image's pixels.
    auto reservation = std::make_shared<ps5::artwork::PixelReservation>(nativeArtworkPixelBudget(), payload.pixelBytes);
    if (!*reservation) { payload.pixelPressure = true; return; }
    if (request->cancelled->load()) return;
    int width = payload.width, height = payload.height;
    const bool webp = payload.webp;
    unsigned char* pixels = nullptr;
#ifdef USE_WEBP
    if (webp) {
        pixels = WebPDecodeRGBA(reinterpret_cast<const uint8_t*>(payload.encoded->data()),
            payload.encoded->size(), &width, &height);
    } else
#endif
    {
        int channels = 0;
        pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(payload.encoded->data()),
            static_cast<int>(payload.encoded->size()), &width, &height, &channels, 4);
    }
    payload.data = std::shared_ptr<unsigned char>(pixels, [webp, reservation](unsigned char* value) {
        if (!value) return;
#ifdef USE_WEBP
        if (webp) { WebPFree(value); return; }
#else
        (void)webp;
#endif
        stbi_image_free(value);
    });
    payload.encoded.reset();
    if (width != payload.width || height != payload.height || request->cancelled->load()) payload.data.reset();
}

ps5::artwork::Delivery completeNativeArtwork(const NativeArtworkRequests::Ref& request) {

    auto& requests = nativeArtworkRequests();
    if (brls::TextureCache::instance().isClosing()) {
        requests.finish(request);
        return ps5::artwork::Delivery::Complete;
    }
    // Retry before sealing the flight: current subscribers retain their exact
    // membership/pin, and new subscribers may still join the same request.
    if (request->payload.pixelPressure) return ps5::artwork::Delivery::Retry;
    // Limit both failed upload bursts and cached fan-out. Deferred members keep
    // their pins and decoded payload until a later run-loop pass or cancellation.
    bool uploadAttempted = false;
    const auto result = requests.completeSome(request, 16, [&](brls::Image* view, const NativeArtworkPixels& pixels) {
        auto& cache = brls::TextureCache::instance();
        if (cache.isClosing()) return true;
        int texture;
        {
            texture = cache.getCache(request->url);
        }
        if (texture == 0 && pixels.data && pixels.width > 0 && pixels.height > 0) {
            if (uploadAttempted) return false;
            uploadAttempted = true; // Count failures/exceptions as attempts too.
            auto* vg = brls::Application::getNVGContext();
            {
                texture = nvgCreateImageRGBA(vg, pixels.width, pixels.height, 0, pixels.data.get());
            }
            if (texture > 0) {
                if (!cache.tryAddCache(request->url, texture)) {
                    nvgDeleteImage(vg, texture);
                    texture = 0;
                }
            }
        }
        if (texture > 0) {
            view->setImageFromCache(texture);
        }
        return true;
    });
    if (result.failures) reportNativeArtworkFailure();
    return result.finished ? ps5::artwork::Delivery::Complete : ps5::artwork::Delivery::Defer;
}

void retireNativeArtwork(const NativeArtworkRequests::Ref& request) noexcept {

    request->payload = {};
    nativeArtworkRequests().finish(request);
}

void pumpNativeArtwork(bool allowDelivery, std::chrono::steady_clock::time_point now) {
    auto& scheduler = nativeArtworkScheduler();
    const auto before = scheduler.snapshot();
    scheduler.pump(allowDelivery,
        [](auto task) { ThreadPool::instance().submit(std::move(task)); },
        [](const NativeArtworkRequests::Ref& request, HTTP& http) { downloadNativeArtwork(request, http); },
        [](const NativeArtworkRequests::Ref& request) noexcept { return request->cancelled->load(); },
        completeNativeArtwork, retireNativeArtwork, now);
    const auto after = scheduler.snapshot();
    if (after.submissionFailures != before.submissionFailures || after.workerFailures != before.workerFailures ||
        after.deliveryFailures != before.deliveryFailures) {
        // A failure logger must not escape the owned completion phase.
        reportNativeArtworkFailure();
    }
}

} // namespace
#endif

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

static inline __attribute__((always_inline)) void extract_block(
    const uint8_t* src, uint32_t stride, uint32_t remaining_w, uint32_t remaining_h, uint8_t* block) {
    // Fast path: a fully in-bounds 4x4 block, copy four contiguous rows.
    if (remaining_w >= 4 && remaining_h >= 4) {
        for (int j = 0; j < 4; j++) {
            memcpy(&block[j * 4 * 4], src, 16);
            src += stride * 4;
        }
        return;
    }
    // Edge block: the image width/height is not a multiple of 4, so this
    // block only partially overlaps the image. Copying a full 4x4 here would
    // read the next row's pixels (right edge) or run past the decoded buffer
    // (bottom edge) — that overread is what shears/corrupts logos with awkward
    // dimensions. Clamp to the valid pixels and replicate the last in-bounds
    // row/column into the padding so the block stays a single colour region
    // (clean edge under bilinear CLAMP sampling, fewer colours for DXT).
    uint32_t copy_w = MIN(remaining_w, 4u);
    uint32_t copy_h = MIN(remaining_h, 4u);
    for (uint32_t y = 0; y < 4; y++) {
        const uint8_t* row = src + (y < copy_h ? y : copy_h - 1) * stride * 4;
        uint8_t* drow = &block[y * 16];
        for (uint32_t x = 0; x < 4; x++) {
            memcpy(&drow[x * 4], row + (x < copy_w ? x : copy_w - 1) * 4, 4);
        }
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
        // offs_x indexes rows (height), offs_y indexes columns (width); both
        // remaining counts are > 0 thanks to the bounds check above.
        extract_block(
            src + offs_y * 16 + offs_x * stride * 16, stride, w - offs_y * 4, h - offs_x * 4, block);
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
    Image::with(view, url, {});
}

void Image::with(brls::Image* view, const std::string& url, const HTTP::Header& headers) {
#ifdef PS5_NATIVE_GPU
    if (!view) return;
    auto& admission = view->artworkAdmission();
    const auto generation = admission.begin(AppConfig::instance().requestCancellation());
    using Result = brls::ArtworkRetry::Result;
    try {
        prepareNativeArtwork();
        auto& requests = nativeArtworkRequests();
        requests.cancel(view);
        auto& cache = brls::TextureCache::instance();
        if (requests.isClosing() || cache.isClosing()) return;
        pumpNativeArtwork(false);
        const auto key = nativeArtworkKey(url, headers);
        if (key.empty()) return;
        const int texture = cache.getCache(key);
        if (texture > 0) {
            view->setImageFromCache(texture);
            return;
        }
        bool start = false;
        const auto request = requests.begin(view, key, &start);
        if (!request) {
            admission.complete(generation, Result::Temporary);
            return;
        }
        if (!start) {
            admission.complete(generation, Result::Accepted);
            return;
        }
        try {
            if (!headers.empty()) {
                request->payload.sourceUrl = url;
                request->payload.headers = headers;
            }
            if (!nativeArtworkScheduler().enqueue(request)) {
                requests.finish(request);
                admission.complete(generation, Result::Temporary);
                return;
            }
            admission.complete(generation, Result::Accepted);
            pumpNativeArtwork(false);
        } catch (...) {
            requests.finish(request);
            reportNativeArtworkFailure();
        }
    } catch (...) {
        // Registration/key/registry allocations precede worker ownership too.
        // Flights rolls back registration before propagating allocation failure.
        reportNativeArtworkFailure();
#else
    int tex = brls::TextureCache::instance().getCache(url);
    if (tex > 0) {
        view->innerSetImage(tex);
        return;
#endif
    }
#ifndef PS5_NATIVE_GPU

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
        // 该 view 已有进行中的请求：归还从池中取出的 item，避免池泄漏
        brls::Logger::warning("insert Image {} failed", fmt::ptr(view));
        item->image = nullptr;
        pool.push_back(item);
        return;
    }

    item->image = view;
    item->url = url;
    item->headers = headers;
    item->isCancel->store(false);
    view->ptrLock();
    // 设置图片组件不处理纹理的销毁，由缓存统一管理纹理销毁
    view->setFreeTexture(false);

    ThreadPool::instance().submit([item](HTTP& s) { item->doRequest(s); });
#endif
}

void Image::cancel(brls::Image* view) {
    if (view == nullptr) return;

#ifdef PS5_NATIVE_GPU
    nativeArtworkRequests().cancel(view);
#else
    brls::TextureCache::instance().removeCache(view->getTexture());
#endif
    view->clear();
#ifdef PS5_NATIVE_GPU
    pumpNativeArtwork(false);
#else

    clear(view);
#endif
}

#ifndef PS5_NATIVE_GPU
void Image::doRequest(HTTP& s) {
    if (this->isCancel->load()) {
        Image::clear(this->image);
        return;
    }
    try {
        std::ostringstream body;
        HTTP::set_option(s, this->isCancel, HTTP::Timeout{});
        if (!this->headers.empty()) HTTP::set_option(s, this->headers);
        s._get(this->url, &body);
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
            if (!isWebp) size >>= 1;
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
        auto imagePtr = this->image;
        auto urlCopy = this->url;
        auto isCancelCopy = this->isCancel;

        brls::Logger::verbose("request Image {} size {}", urlCopy, data.size());
        brls::sync([imagePtr, urlCopy, isCancelCopy, imageData, imageW, imageH, isWebp] {
            if (!isCancelCopy->load()) {
                // Load texture
                int tex = brls::TextureCache::instance().getCache(urlCopy);
                if (tex == 0 && imageData != nullptr) {
                    NVGcontext* vg = brls::Application::getNVGContext();
#ifdef BOREALIS_USE_GXM
                    tex = nvgCreateImageRGBA(
                        vg, imageW, imageH, (isWebp ? NVG_IMAGE_DXT5 : NVG_IMAGE_DXT1) | NVG_IMAGE_LPDDR, imageData);
#else
                    tex = nvgCreateImageRGBA(vg, imageW, imageH, 0, imageData);
#endif
                    brls::TextureCache::instance().addCache(urlCopy, tex);
                }
                if (tex > 0) imagePtr->innerSetImage(tex);
                clear(imagePtr);
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
    if (view == nullptr) return;

    std::lock_guard<std::mutex> lock(requestMutex);

    auto it = requests.find(view);
    if (it == requests.end()) return;

    // 归属校验：防止已被 cancel/复用的旧任务回调误清空新请求
    if (it->second->image != view) return;

    it->second->image->ptrUnlock();
    it->second->image = nullptr;
    it->second->isCancel->store(true);
    pool.push_back(it->second);
    requests.erase(it);
}
#endif
