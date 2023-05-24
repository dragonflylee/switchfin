#include "utils/image.hpp"
#include <fmt/format.h>
#include <borealis/core/cache_helper.hpp>

std::unordered_map<brls::Image*, Image::ImageRef> Image::requests;

Image::Image(brls::Image* view) : image(view) {
    this->isCancel = std::make_shared<std::atomic_bool>(false);

    view->ptrLock();
    // 设置图片组件不处理纹理的销毁，由缓存统一管理纹理销毁
    view->setFreeTexture(false);

    brls::Logger::debug("new Image {}", fmt::ptr(this));
}

Image::~Image() {
    if (this->image) this->image->ptrUnlock();
    brls::Logger::debug("delete Image {}", fmt::ptr(this));
}

void Image::clear() { requests.erase(this->image); }

void Image::load(brls::Image* view, const std::string& url) {
    int tex = brls::TextureCache::instance().getCache(url);
    if (tex > 0) {
        view->innerSetImage(tex);
        return;
    }

    ImageRef item = std::make_shared<Image>(view);
    item->url = url;
    auto it = requests.insert(std::make_pair(view, item));
    if (it.second) brls::async(std::bind(&Image::doRequest, item));
}

void Image::cancel(brls::Image* view) {
    auto it = requests.find(view);
    if (it != requests.end()) it->second->isCancel->store(true);
}

void Image::doRequest() {
    try {
        std::string data = HTTP::get(this->url, this->isCancel);
        brls::Logger::debug("request Image {} size {}", this->url, data.size());
        brls::sync([this, data] {
            // Load texture
            int tex = brls::TextureCache::instance().getCache(url);
            if (tex == 0) {
                NVGcontext* vg = brls::Application::getNVGContext();
                tex = nvgCreateImageMem(vg, 0, (unsigned char*)data.c_str(), data.size());
                brls::TextureCache::instance().addCache(this->url, tex);
            }
            if (tex > 0) {
                brls::TextureCache::instance().removeCache(this->image->getTexture());
                this->image->innerSetImage(tex);
            }
            this->clear();
        });
    } catch (const std::exception& ex) {
        brls::sync(std::bind(&Image::clear, this));
    }
}