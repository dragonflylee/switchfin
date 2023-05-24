#pragma once

#include <borealis.hpp>
#include "api/http.hpp"

class Image {
    using ImageRef = std::shared_ptr<Image>;

public:
    Image(brls::Image* view);
    Image(const Image&) = delete;

    virtual ~Image();

    /// @brief 设置要加载内容的图片组件。此函数需要工作在主线程。
    static void load(brls::Image* view, const std::string& url);

    /// @brief 取消请求，并清空图片。此函数需要工作在主线程。
    static void cancel(brls::Image* view);

private:
    void doRequest();

    void clear();

private:
    std::string url;
    brls::Image* image;
    HTTP::Cancel isCancel;

    /// 对象池
    static std::unordered_map<brls::Image*, ImageRef> requests;
};