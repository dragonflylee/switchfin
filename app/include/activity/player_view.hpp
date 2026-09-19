/*
    Copyright 2024 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/jellyfin/media.hpp>
#ifdef PS5_NATIVE_GPU
#include <api/jellyfin.hpp>
#include <utils/playback_report.hpp>
#include <memory>
#include <functional>
#include <utils/ps5_playback_request.hpp>
#include <utils/ps5_playback_admission.hpp>
#include <utils/ps5_compatible_url.hpp>
#endif

class VideoView;

class PlayerView : public brls::Box {
public:
    PlayerView(const jellyfin::Item& item, const uint64_t seekTicks = 0, const std::string& = "");
    ~PlayerView();
#ifdef PS5_NATIVE_GPU
    void frame(brls::FrameContext* ctx) override;
#endif

    void setSeries(const std::string& seriesId);
    void setTitie(const std::string& title);

#ifdef ANDROID
    void willDisappear(bool resetState) override {
        if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(235, 235, 235, 255));
        else
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(45, 45, 45, 255));
    }

    void willAppear(bool resetState) override {
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

private:
    void setChapters(const std::vector<jellyfin::MediaChapter>& chaps, uint64_t duration);
    /// @brief get video url
#ifdef PS5_NATIVE_GPU
    void playMedia(const uint64_t seekTicks, bool compatible = false);
#else
    void playMedia(const uint64_t seekTicks);
#endif
    bool playIndex(int index);
    void reportStart();
    void reportStop();
#ifdef PS5_NATIVE_GPU
    void reportPlay(std::optional<bool> isPaused = std::nullopt);
    void sendReport(std::string_view endpoint, nlohmann::json data, bool stopping = false);
#else
    void reportPlay(bool isPaused = false);
#endif
    void requestDanmaku();
    bool toggleQuality();
#ifdef PS5_NATIVE_GPU
    void deferLoadFailure(const std::string& message);
    void pumpLoadFailure();
#endif

    // Playinfo
    std::string itemId;
    std::string sourceId;
    /// @brief DirectPlay, Transcode
    std::string playMethod;
    std::string playSessionId;
    jellyfin::Source stream;
#ifdef PS5_NATIVE_GPU
    jellyfin::PlaybackReport reporting;
    std::optional<jellyfin::RequestContext> reportContext;
    struct PlaybackLifetime { uint64_t generation = 0; bool active = true; };
    std::shared_ptr<PlaybackLifetime> playbackLifetime = std::make_shared<PlaybackLifetime>();
    bool playbackReady = false;
    struct FailureIntent { std::string message; std::function<bool()> valid; bool queued = false; };
    std::shared_ptr<FailureIntent> failureIntent;
    ps5::playback::LatestRequest playbackRequest;
    ps5::playback::Fallback fallback;
    bool retryCompatible(std::optional<uint64_t> ticks = std::nullopt);
#endif
    std::vector<jellyfin::Episode> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
