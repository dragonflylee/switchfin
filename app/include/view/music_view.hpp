//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#ifdef PS5_NATIVE_GPU
#include <api/jellyfin.hpp>
#include <utils/playback_report.hpp>
#include <chrono>
#else
#include <api/jellyfin/media.hpp>
#endif
#include <client/client.hpp>
#include <utils/event.hpp>
#ifdef PS5_NATIVE_GPU
#include <memory>
#endif

class VideoProgressSlider;
class SVGImage;

class MusicView : public brls::Box, public brls::Singleton<MusicView> {
    enum RepeatMode { RepeatNone, RepeatOne, RepeatAll };

public:
    MusicView();
    ~MusicView() override;

    bool isTranslucent() override { return true; }

    void registerViewAction(brls::View* view);

    const std::string& currentId();

    void image(brls::Image *image);

    void load(const std::vector<jellyfin::Track>& items, size_t index);

    void load(const std::vector<remote::DirEntry>& items, size_t index, const std::string& extra);

public:
    struct Track {
        std::string Id;
        std::string Title;
        std::string Album;
        std::string ImageId;
        std::string ImageTag;
#ifdef PS5_NATIVE_GPU
        std::string PlaySessionId;
        std::string StreamUrl;
#endif

#ifdef PS5_NATIVE_GPU
        explicit Track(const jellyfin::Track& item) : Id(item.Id), Title(item.Name) {
            this->Album = item.Album;
            this->ImageId = item.AlbumId;
            this->ImageTag = item.AlbumPrimaryImageTag;
#else
        Track(jellyfin::Track* item) : Id(item->Id), Title(item->Name) {
            this->Album = item->Album;
            this->ImageId = item->AlbumId;
            this->ImageTag = item->AlbumPrimaryImageTag;
#endif
        }
    };

private:
    BRLS_BIND(brls::Box, btnPrev, "music/prev");
    BRLS_BIND(brls::Box, btnNext, "music/next");
    BRLS_BIND(brls::Box, btnToggle, "music/toggle");
    BRLS_BIND(brls::Box, btnSuffle, "music/shuffle");
    BRLS_BIND(brls::Box, btnRepeat, "music/repeat");
    BRLS_BIND(SVGImage, btnRepeatIcon, "music/repeat/icon");
    BRLS_BIND(SVGImage, btnToggleIcon, "music/toggle/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "music/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "music/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "music/right/status");
    BRLS_BIND(brls::Label, playTitle, "music/play/title");

    bool toggleShuffle();

    bool toggleLoop();

    void registerMpvEvent();

    void unregisterMpvEvent();

#ifdef PS5_NATIVE_GPU
    void deferIdleTeardown();

    void beginLoad(size_t count, size_t index);

    void finishLoad();

#endif
    void reset();
#ifdef PS5_NATIVE_GPU
    const Track* playingTrack() const;
    void handleReport(MpvEventEnum event);
    void stopReport(bool failed = false);
    void scheduleReportCheck();
    void sendMusicReport(std::string_view endpoint, nlohmann::json data, bool stopping = false);
    void updateProgress();
#endif

#ifdef PS5_NATIVE_GPU
    MPVEvent::Subscription eventSubscribeID{};
    MPVCommandReply::Subscription replySubscribeID{};
    brls::VoidEvent::Subscription exitSubscribeID{};
    bool subscriptionsActive = false;
    struct CallbackLifetime {
        uint64_t generation = 0;
        bool active = true;
        bool teardownPending = false;
        bool reportCheckPending = false;
    };
    std::shared_ptr<CallbackLifetime> callbackLifetime = std::make_shared<CallbackLifetime>();
#else
    MPVEvent::Subscription eventSubscribeID;
    MPVCommandReply::Subscription replySubscribeID;
#endif

    using MusicList = std::unordered_map<int64_t, Track>;
    int64_t playSession = 0;
    std::string itemId;
#ifdef PS5_NATIVE_GPU
    int64_t displayEntry = -1;
#endif
    MusicList playList;
#ifdef PS5_NATIVE_GPU
    // mpv replies outlive the caller's vector and can arrive after a new load.
    // Keep copied metadata behind opaque IDs; never send borrowed pointers.
    std::unordered_map<uint64_t, Track> pendingTracks;
    std::unordered_map<uint64_t, size_t> pendingLoads;
    std::vector<int64_t> loadedEntries;
    size_t requestedIndex = 0;
    bool submittingLoads = false;
    bool startPending = false;
    uint64_t loadPlaylistGeneration = 0;
    uint64_t nextCommandId = 1;

    std::optional<jellyfin::RequestContext> playlistReportContext, activeReportContext;
    jellyfin::PlaybackReport reporting;
    int64_t reportEntry = -1;
    std::string reportUrl;
    double reportPosition = 0;
    bool reportPaused = false, reportSeekable = false;
    std::chrono::steady_clock::time_point lastReportAt{};
#endif

    RepeatMode repeat = RepeatNone;
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
