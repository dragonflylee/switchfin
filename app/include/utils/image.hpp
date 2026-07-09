#pragma once

#include <borealis.hpp>
#include "api/http.hpp"
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
        // (SPEC §4.2, AC6/AC17). Keyed by the raw path/url below.
        if (ImageCache::has(path)) {
            view->setImageFromFile(ImageCache::localPath(path));
            return;
        }
        // external agent paths (cast faces...) are absolute
        if (path.rfind("http", 0) == 0) return with(view, path);
        auto& conf = AppConfig::instance();
        std::string url;
        if (width > 0 || height > 0) {
            // the photo transcoder ALWAYS expects both dimensions;
            // square box by default — minSize=1 preserves the ratio by
            // covering the box
            if (width <= 0) width = height;
            if (height <= 0) height = width;
            HTTP::Form form = {
                {"minSize", "1"},
                {"upscale", "1"},
                {"url", path + "?X-Plex-Token=" + conf.getToken()},
                {"X-Plex-Token", conf.getToken()},
            };
            form["width"] = std::to_string(width);
            form["height"] = std::to_string(height);
            url = conf.getUrl() + "/photo/:/transcode?" + HTTP::encode_form(form);
        } else {
            url = conf.getUrl() + path + "?X-Plex-Token=" + conf.getToken();
        }
        return with(view, url);
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