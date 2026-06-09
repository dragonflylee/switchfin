#include "view/video_profile.hpp"
#include "utils/misc.hpp"
#include <fmt/ranges.h>

VideoProfile::VideoProfile() {
    this->inflateFromXMLRes("xml/view/video_profile.xml");
    brls::Logger::debug("View VideoProfile: create");
    this->setVisibility(brls::Visibility::INVISIBLE);
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(25);
    this->setPositionLeft(25);
    // statistiques de transcodage serveur : non disponibles sans session
    // d'admin Plex — panneau local mpv uniquement
    this->boxTranscode->setVisibility(brls::Visibility::GONE);
}

VideoProfile::~VideoProfile() { brls::Logger::debug("View VideoProfile: delete"); }

#include "view/mpv_core.hpp"

void VideoProfile::init(const std::string& method) {
    auto& mpv = MPVCore::instance();
    labelUrl->setText(mpv.getString("path"));
    labelFormat->setText(mpv.getString("file-format"));
    labelSize->setText(misc::formatSize(mpv.getInt("file-size")));

    if (method.empty())
        labelMethod->setText(mpv.getString("playlist-path"));
    else
        labelMethod->setText(method);

    this->inited = true;
    this->update();
}

void VideoProfile::update() {
    if (!this->inited) return;

    auto& mpv = MPVCore::instance();
    // video
    labelVideoRes->setText(
        fmt::format("{} x {}@{} (window: {} x {} framebuffer: {} x {})", mpv.getInt("video-params/w"),
            mpv.getInt("video-params/h"), mpv.getInt("container-fps"), brls::Application::contentWidth,
            brls::Application::contentHeight, brls::Application::windowWidth, brls::Application::windowHeight));
    labelVideoCodec->setText(mpv.getString("video-codec"));
    labelVideoPixel->setText(mpv.getString("video-params/pixelformat"));
    labelVideoHW->setText(mpv.getString("hwdec-current"));

    auto cache = mpv.getNodeMap("demuxer-cache-state");
    double duration = mpv.getDouble("demuxer-cache-duration");
    labelCache->setText(fmt::format("{:.2f}MB ({:.1f} sec)", cache["fw-bytes"].u.int64 / 1048576.0f, duration));
    labelVideoBitrate->setText(fmt::format("{} kbps", mpv.getInt("video-bitrate") / 1024));
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
        labelSubTrack->setText(fmt::format("{} {}", subId, mpv.getString("current-tracks/sub/title")));
        labelSubCodec->setText(mpv.getString("current-tracks/sub/codec"));
        boxSubtitle->setVisibility(brls::Visibility::VISIBLE);
    } else {
        boxSubtitle->setVisibility(brls::Visibility::GONE);
    }
}

