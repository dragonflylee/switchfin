#pragma once

#include <borealis.hpp>

class VideoProfile : public brls::Box {
public:
    VideoProfile();
    ~VideoProfile() override;

    void init(const std::string& title, const std::string& method);
    void update();

    BRLS_BIND(brls::Label, labelUrl, "profile/file/url");
    BRLS_BIND(brls::Label, labelMethod, "profile/play/method");
    BRLS_BIND(brls::Label, labelSize, "profile/file/size");
    BRLS_BIND(brls::Label, labelFormat, "profile/file/format");
    BRLS_BIND(brls::Label, labelCache, "profile/file/cache");

    BRLS_BIND(brls::Label, labelVideoRes, "profile/video/res");
    BRLS_BIND(brls::Label, labelVideoCodec, "profile/video/codec");
    BRLS_BIND(brls::Label, labelVideoPixel, "profile/video/pixel");
    BRLS_BIND(brls::Label, labelVideoHW, "profile/video/hwdec");
    BRLS_BIND(brls::Label, labelVideoBitrate, "profile/video/bitrate");
    BRLS_BIND(brls::Label, labelVideoDrop, "profile/video/drop");
    BRLS_BIND(brls::Label, labelVideoSync, "profile/video/avsync");

    BRLS_BIND(brls::Label, labelAudioChannel, "profile/audio/channel");
    BRLS_BIND(brls::Label, labelAudioCodec, "profile/audio/codec");
    BRLS_BIND(brls::Label, labelAudioSampleRate, "profile/audio/sample");
    BRLS_BIND(brls::Label, labelAudioBitrate, "profile/audio/bitrate");
};