#pragma once

#include <borealis.hpp>
#include "api/http.hpp"
#include "api/backend.hpp"
#include "config.hpp"
#include "image_cache.hpp"

class Image {
    using Ref = std::shared_ptr<Image>;

public:
    Image();
    Image(const Image&) = delete;

    virtual ~Image();

    /// Loads an image from the Plex server given a relative path (thumb/art...).
    /// width/height > 0 -> server-side resize via /photo/:/transcode
    /// (PLEX_MIGRATION.md §2.5).
    static void load(brls::Image* view, const std::string& path, int width = 0, int height = 0) {
        if (path.empty()) return;
        // offline cache wins: a locally cached asset renders without the server
        // and gives downloaded content instant local artwork even online
        // (SPEC §4.2, AC6/AC17). Keyed by the raw path/url passed here.
        if (ImageCache::has(path)) {
            view->setImageFromFile(ImageCache::localPath(path));
            return;
        }
        // backend-specific URL building (Plex /photo/:/transcode, Jellyfin /Images...);
        // absolute external paths (cast faces...) are returned unchanged by the backend
        std::string url = AppConfig::instance().backend().imageUrl(path, width, height);
        if (!url.empty()) with(view, url);
    }

    /// @brief 设置要加载内容的图片组件。此函数需要工作在主线程。
    static void with(brls::Image* view, const std::string& url);

    /// @brief 取消请求，并清空图片。此函数需要工作在主线程。
    static void cancel(brls::Image* view);

private:
    void doRequest(HTTP& s);

    static void clear(brls::Image* view);

private:
    std::string url;
    brls::Image* image;
    HTTP::Cancel isCancel;

    /// 对象池
    inline static std::list<Ref> pool;
    inline static std::mutex requestMutex;
    inline static std::unordered_map<brls::Image*, Ref> requests;
};