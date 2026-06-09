#pragma once

#include <borealis.hpp>
#include "api/http.hpp"
#include "config.hpp"

class Image {
    using Ref = std::shared_ptr<Image>;

public:
    Image();
    Image(const Image&) = delete;

    virtual ~Image();

    /// Charge une image du serveur Plex depuis un chemin relatif (thumb/art…).
    /// width/height > 0 → redimensionnement serveur via /photo/:/transcode
    /// (PLEX_MIGRATION.md §2.5 ; plex_client.dart:4019-4056).
    static void load(brls::Image* view, const std::string& path, int width = 0, int height = 0) {
        if (path.empty()) return;
        // les chemins d'agents externes (visages du casting…) sont absolus
        if (path.rfind("http", 0) == 0) return with(view, path);
        auto& conf = AppConfig::instance();
        std::string url;
        if (width > 0 || height > 0) {
            // le transcodeur photo attend TOUJOURS les deux dimensions (plezy
            // les calcule ensemble, media_image_helper.dart:197) ; boîte carrée
            // par défaut — minSize=1 préserve le ratio en couvrant la boîte
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