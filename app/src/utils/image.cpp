#include "utils/image.hpp"
#include "utils/thread.hpp"
#include <fmt/format.h>
#include <borealis/core/cache_helper.hpp>

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

    ThreadPool::instance().submit(std::bind(&Image::doRequest, item));
}

void Image::cancel(brls::Image* view) {
    brls::TextureCache::instance().removeCache(view->getTexture());
    view->clear();

    clear(view);
}

void Image::doRequest() {
    if (this->isCancel->load()) {
        brls::sync(std::bind(&Image::clear, this->image));
        return;
    }
    try {
        std::string data = HTTP::get(this->url, this->isCancel);
        brls::Logger::verbose("request Image {} size {}", this->url, data.size());
        brls::sync([this, data] {
            if (this->isCancel->load()) return;
            // Load texture
            int tex = brls::TextureCache::instance().getCache(url);
            if (tex == 0) {
                NVGcontext* vg = brls::Application::getNVGContext();
                tex = nvgCreateImageMem(vg, 0, (unsigned char*)data.c_str(), data.size());
                brls::TextureCache::instance().addCache(this->url, tex);
            }
            if (tex > 0) this->image->innerSetImage(tex);

            clear(this->image);
        });
    } catch (const std::exception& ex) {
        brls::Logger::warning("request image {} {}", this->url, ex.what());
        brls::sync(std::bind(&Image::clear, this->image));
    }
}

void Image::clear(brls::Image* view) {
    auto it = requests.find(view);
    if (it == requests.end()) return;

    it->second->image->ptrUnlock();
    it->second->image = nullptr;
    it->second->isCancel->store(true);
    pool.push_back(it->second);
    requests.erase(it);
}