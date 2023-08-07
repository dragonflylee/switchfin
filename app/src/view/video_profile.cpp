#include "view/video_profile.hpp"
#include "api/jellyfin/media.hpp"

VideoProfile::VideoProfile() {
    this->inflateFromXMLRes("xml/view/video_profile.xml");
    brls::Logger::debug("View VideoProfile: create");
    this->setVisibility(brls::Visibility::INVISIBLE);
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(25);
    this->setPositionLeft(25);
}

VideoProfile::~VideoProfile() { brls::Logger::debug("View VideoProfile: delete"); }

#include "view/mpv_core.hpp"

void VideoProfile::init(const std::string& title, const std::string& method) {
    auto& mpv = MPVCore::instance();
    labelUrl->setText(title);
    labelMethod->setText(method);
    labelFormat->setText(mpv.getString("file-format"));
    labelSize->setText(fmt::format("{:.2f}MB", mpv.getInt("file-size") / 1048576.0));
    this->update();
}

void VideoProfile::update() {
    auto& mpv = MPVCore::instance();
    // video
    labelVideoRes->setText(
        fmt::format("{} x {}@{} (window: {} x {} framebuffer: {} x {})", mpv.getInt("video-params/w"),
            mpv.getInt("video-params/h"), mpv.getInt("container-fps"), brls::Application::contentWidth,
            brls::Application::contentHeight, brls::Application::windowWidth, brls::Application::windowHeight));
    labelVideoCodec->setText(mpv.getString("video-codec"));
    labelVideoPixel->setText(mpv.getString("video-params/pixelformat"));
    labelVideoHW->setText(mpv.getString("hwdec-current"));
    labelVideoBitrate->setText(std::to_string(mpv.getInt("video-bitrate") / 1024.0f) + "kbps");
    labelVideoDrop->setText(fmt::format(
        "{} (decoder) {} (output)", mpv.getInt("decoder-frame-drop-count"), mpv.getInt("frame-drop-count")));
    labelVideoFps->setText(fmt::format("{:.2f}", mpv.getDouble("estimated-vf-fps")));
    labelVideoSync->setText(fmt::format("{:.5f}", mpv.getDouble("avsync")));

    // audio
    labelAudioCodec->setText(mpv.getString("audio-codec"));
    labelAudioChannel->setText(mpv.getString("audio-params/channel-count"));
    labelAudioSampleRate->setText(std::to_string(mpv.getInt("audio-params/samplerate") / 1000) + "kHz");
    labelAudioBitrate->setText(std::to_string(mpv.getInt("audio-bitrate") / 1024) + "kbps");

    // subtitle
    labelSubtitleTrack->setText(std::to_string(mpv.getInt("sid")));
    labelSubtitleSpeed->setText(std::to_string(mpv.getDouble("sub-speed")));
}