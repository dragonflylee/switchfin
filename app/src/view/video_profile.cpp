#include "view/video_profile.hpp"
#include <fmt/ranges.h>

VideoProfile::VideoProfile() {
    this->inflateFromXMLRes("xml/view/video_profile.xml");
    brls::Logger::debug("View VideoProfile: create");
    this->setVisibility(brls::Visibility::INVISIBLE);
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(25);
    this->setPositionLeft(25);
    this->boxTranscode->setVisibility(brls::Visibility::GONE);

    this->ticker.setCallback([this]() { this->onRequest(); });
}

VideoProfile::~VideoProfile() {
    brls::Logger::debug("View VideoProfile: delete");
    this->ticker.stop();
}

#include "view/mpv_core.hpp"
#include "api/jellyfin.hpp"

void VideoProfile::init(const std::string& title, const std::string& method) {
    auto& mpv = MPVCore::instance();
    labelUrl->setText(title);
    labelMethod->setText(method);
    labelFormat->setText(mpv.getString("file-format"));
    labelSize->setText(fmt::format("{:.2f}MB", mpv.getInt("file-size") / 1048576.0));
    if (method == jellyfin::methodTranscode)
        ticker.start(2000);
    else
        this->ticker.stop();
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
    labelVideoBitrate->setText(std::to_string(mpv.getInt("video-bitrate") / 1024) + "kbps");
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
    int subId = mpv.getInt("sid");
    if (subId > 0) {
        labelSubtitleTrack->setText(fmt::format("{} SrcID {} ", subId, mpv.getString("current-tracks/sub/src-id")));
        boxSubtitle->setVisibility(brls::Visibility::VISIBLE);
    } else {
        boxSubtitle->setVisibility(brls::Visibility::GONE);
    }
}

void VideoProfile::onRequest() {
    std::string query = HTTP::encode_form({
        {"deviceId", AppConfig::instance().getDeviceId()},
    });
    jellyfin::getJSON(
        [this](const std::vector<jellyfin::SessionInfo>& list) {
            if (list.empty()) return;

            auto& s = list.front();
            if (s.PlayState.PlayMethod == jellyfin::methodTranscode) {
                this->boxTranscode->setVisibility(brls::Visibility::VISIBLE);
                this->labelTranscodePercent->setText(fmt::format("{:.5f}", s.TranscodingInfo.CompletionPercentage));
                this->labelTranscodeReasons->setText(
                    fmt::format("{}", fmt::join(s.TranscodingInfo.TranscodeReasons, "\n")));
            } else {
                this->boxTranscode->setVisibility(brls::Visibility::GONE);
            }
        },
        [](const std::string& ex) { brls::Logger::warning("query session {}", ex); }, jellyfin::apiSessionList, query);
}