#pragma once

#include <borealis.hpp>
#include "api/http.hpp"

class Image {
    using Ref = std::shared_ptr<Image>;
    using Pool = std::unordered_map<brls::Image*, Ref>;

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

private:
    std::string url;
    brls::Image* image;
    HTTP::Cancel isCancel;

    /// 对象池
    static Pool requests;
};