//
// Created by fang on 2022/4/22.
//

#pragma once

#include <borealis.hpp>

class SVGImage;

// https://github.com/mpv-player/mpv/blob/master/DOCS/edl-mpv.rst
class EDLUrl {
public:
    std::string url;
    float length = -1;  // second

    EDLUrl(std::string url, float length = -1) : url(url), length(length) {}
};

enum class OSDState {
    HIDDEN = 0,
    SHOWN = 1,
    ALWAYS_ON = 2,
};

class VideoView : public brls::Box {
public:
    VideoView();

    ~VideoView() override;

    /// Video control
    void setUrl(const std::string& url, int progress = 0, const std::string& audio = "");

private:
    ///OSD
    BRLS_BIND(brls::Label, videoTitleLabel, "video/osd/title");
};
