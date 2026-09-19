#include "api/jellyfin.hpp"
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
    this->boxTranscode->setVisibility(brls::Visibility::GONE);

    this->ticker.setCallback([this]() { this->onRequest(); });
}

VideoProfile::~VideoProfile() {
    brls::Logger::debug("View VideoProfile: delete");
    this->ticker.stop();
}

#include "view/mpv_core.hpp"

void VideoProfile::init(const std::string& method) {
#ifdef PS5_NATIVE_GPU
    this->playMethod = method;
#else
    auto& mpv = MPVCore::instance();
    labelUrl->setText(mpv.getString("path"));
    labelFormat->setText(mpv.getString("file-format"));
    labelSize->setText(misc::formatSize(mpv.getInt("file-size")));

    if (method.empty())
        labelMethod->setText(mpv.getString("playlist-path"));
    else
        labelMethod->setText(method);
#endif

    if (method == jellyfin::methodTranscode)
        ticker.start(2000);
#ifdef PS5_NATIVE_GPU
    else {
#else
    else
#endif
        this->ticker.stop();
#ifdef PS5_NATIVE_GPU
        this->boxTranscode->setVisibility(brls::Visibility::GONE);
    }
#endif

    this->inited = true;
    this->update();
}

void VideoProfile::update() {
#ifdef PS5_NATIVE_GPU
    // Property observations populate this snapshot before the profile opens.
    // Updating labels, including metadata after a stream change, never waits
    // for mpv's playback thread while that thread may be waiting for a frame.
    if (!this->inited || this->getVisibility() != brls::Visibility::VISIBLE) return;
#else
    if (!this->inited) return;
#endif

#ifdef PS5_NATIVE_GPU
    const auto& properties = MPVCore::instance().getProfileProperties();
    labelUrl->setText(properties.text("path"));
    labelFormat->setText(properties.text("file-format"));
    labelSize->setText(misc::formatSize(properties.integer("file-size")));
    labelMethod->setText(this->playMethod.empty() ? properties.text("playlist-path") : this->playMethod);
#else
    auto& mpv = MPVCore::instance();
#endif
    // video
    labelVideoRes->setText(
#ifdef PS5_NATIVE_GPU
        fmt::format("{} x {}@{:.3f} (window: {} x {} framebuffer: {} x {})", properties.integer("video-params/w"),
            properties.integer("video-params/h"), properties.number("container-fps"), brls::Application::contentWidth,
#else
        fmt::format("{} x {}@{} (window: {} x {} framebuffer: {} x {})", mpv.getInt("video-params/w"),
            mpv.getInt("video-params/h"), mpv.getInt("container-fps"), brls::Application::contentWidth,
#endif
            brls::Application::contentHeight, brls::Application::windowWidth, brls::Application::windowHeight));
#ifdef PS5_NATIVE_GPU
    labelVideoCodec->setText(properties.text("video-codec"));
    labelVideoPixel->setText(properties.text("video-params/pixelformat"));
    labelVideoHW->setText(properties.text("hwdec-current"));
#else
    labelVideoCodec->setText(mpv.getString("video-codec"));
    labelVideoPixel->setText(mpv.getString("video-params/pixelformat"));
    labelVideoHW->setText(mpv.getString("hwdec-current"));
#endif

#ifdef PS5_NATIVE_GPU
    const auto bytes = properties.integer("demuxer-cache-state/total-bytes", -1);
    const auto duration = properties.number("demuxer-cache-duration", -1);
    const auto cacheSize = bytes >= 0 ? fmt::format("{:.2f} MiB", bytes / 1048576.0) : "— MiB";
    const auto cacheTime = duration >= 0 ? fmt::format("~{:.1f} sec", duration) : "— sec";
    labelCache->setText(fmt::format("{} ({})", cacheSize, cacheTime));
    labelVideoBitrate->setText(fmt::format("{} kbps", properties.integer("video-bitrate") / 1024));
#else
    auto cache = mpv.getNodeMap("demuxer-cache-state");
    double duration = mpv.getDouble("demuxer-cache-duration");
    labelCache->setText(fmt::format("{:.2f}MB ({:.1f} sec)", cache["fw-bytes"].u.int64 / 1048576.0f, duration));
    labelVideoBitrate->setText(fmt::format("{} kbps", mpv.getInt("video-bitrate") / 1024));
#endif
    labelVideoDrop->setText(fmt::format(
#ifdef PS5_NATIVE_GPU
        "{} (decoder) {} (output)", properties.integer("decoder-frame-drop-count"), properties.integer("frame-drop-count")));
    labelVideoFps->setText(fmt::format("{:.2f}", properties.number("estimated-vf-fps")));
    labelVideoSync->setText(fmt::format("{:.5f}", properties.number("avsync")));
#else
        "{} (decoder) {} (output)", mpv.getInt("decoder-frame-drop-count"), mpv.getInt("frame-drop-count")));
    labelVideoFps->setText(fmt::format("{:.2f}", mpv.getDouble("estimated-vf-fps")));
    labelVideoSync->setText(fmt::format("{:.5f}", mpv.getDouble("avsync")));
#endif

    // audio
#ifdef PS5_NATIVE_GPU
    labelAudioCodec->setText(properties.text("audio-codec"));
    labelAudioChannel->setText(std::to_string(properties.integer("audio-params/channel-count")));
    labelAudioSampleRate->setText(std::to_string(properties.integer("audio-params/samplerate") / 1000) + "kHz");
    labelAudioBitrate->setText(std::to_string(properties.integer("audio-bitrate") / 1024) + "kbps");
#else
    labelAudioCodec->setText(mpv.getString("audio-codec"));
    labelAudioChannel->setText(mpv.getString("audio-params/channel-count"));
    labelAudioSampleRate->setText(std::to_string(mpv.getInt("audio-params/samplerate") / 1000) + "kHz");
    labelAudioBitrate->setText(std::to_string(mpv.getInt("audio-bitrate") / 1024) + "kbps");
#endif

    // subtitle
#ifdef PS5_NATIVE_GPU
    int64_t subId = properties.integer("sid");
#else
    int subId = mpv.getInt("sid");
#endif
    if (subId > 0) {
#ifdef PS5_NATIVE_GPU
        labelSubTrack->setText(fmt::format("{} {}", subId, properties.text("current-tracks/sub/title")));
        labelSubCodec->setText(properties.text("current-tracks/sub/codec"));
#else
        labelSubTrack->setText(fmt::format("{} {}", subId, mpv.getString("current-tracks/sub/title")));
        labelSubCodec->setText(mpv.getString("current-tracks/sub/codec"));
#endif
        boxSubtitle->setVisibility(brls::Visibility::VISIBLE);
    } else {
        boxSubtitle->setVisibility(brls::Visibility::GONE);
    }
}

void VideoProfile::onRequest() {
#ifdef PS5_NATIVE_GPU
    if (this->getVisibility() != brls::Visibility::VISIBLE) return;
#endif
    std::string query = HTTP::encode_form({
        {"deviceId", AppConfig::instance().getDeviceId()},
    });
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::SessionInfo>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::SessionInfo>& list) {
            ASYNC_RELEASE
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
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiSessionList, query);
#ifdef PS5_NATIVE_GPU
}
#else
}
#endif
