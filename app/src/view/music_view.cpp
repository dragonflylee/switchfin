#include "view/music_view.hpp"
#include "view/mpv_core.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "utils/config.hpp"
#include "utils/keybind.hpp"
#include "utils/misc.hpp"
#include "utils/image.hpp"
#include "api/http.hpp"
#ifdef PS5_NATIVE_GPU
#include "api/jellyfin_report_queue.hpp"
#include <algorithm>
#include <cmath>
#endif

using namespace brls::literals;

MusicView::MusicView() {
    this->inflateFromXMLRes("xml/view/music_view.xml");
    brls::Logger::debug("MusicView: create");

    auto& mpv = MPVCore::instance();

    /// 播放控制
#ifdef PS5_NATIVE_GPU
    this->btnToggle->registerClickAction([this, &mpv](...) {
        if (startPending) return true;
#else
    this->btnToggle->registerClickAction([&mpv](...) {
#endif
        if (mpv.isStopped())
            mpv.command("playlist-play-index", "current");
        else
            mpv.togglePlay();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

#ifdef PS5_NATIVE_GPU
    this->btnPrev->registerClickAction([this, &mpv](...) {
        if (startPending) return true;
#else
    this->btnPrev->registerClickAction([&mpv](...) {
#endif
        mpv.command("playlist-prev");
        return true;
    });
    this->btnPrev->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnPrev));

#ifdef PS5_NATIVE_GPU
    this->btnNext->registerClickAction([this, &mpv](...) {
        if (startPending) return true;
#else
    this->btnNext->registerClickAction([&mpv](...) {
#endif
        mpv.command("playlist-next");
        return true;
    });
    this->btnNext->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnNext));

    this->btnSuffle->registerClickAction([this](...) { return this->toggleShuffle(); });
    this->btnSuffle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSuffle));

    this->btnRepeat->registerClickAction([this](...) { return this->toggleLoop(); });
    this->btnRepeat->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnRepeat));

    osdSlider->getProgressSetEvent().subscribe([](float progress) {
#ifdef PS5_NATIVE_GPU
        if (!std::isfinite(progress) || progress < 0 || progress > 1 ||
            !std::isfinite(static_cast<double>(MPVCore::instance().duration)) || MPVCore::instance().duration <= 0) return;
#endif
        brls::Logger::verbose("Set progress: {}", progress);
        MPVCore::instance().seek(progress * 100, "absolute-percent");
    });
}

#ifdef PS5_NATIVE_GPU
MusicView::~MusicView() {
    callbackLifetime->active = false;
    this->unregisterMpvEvent();
    brls::Logger::debug("MusicView: delete");
}

void MusicView::beginLoad(size_t count, size_t index) {
    this->stopReport();
    playlistReportContext.reset();
    ++callbackLifetime->generation;
    callbackLifetime->teardownPending = false;
    pendingLoads.clear();
    loadedEntries.assign(count, -1);
    requestedIndex = index;
    submittingLoads = true;
    startPending = true;
    if (!subscriptionsActive) this->registerMpvEvent();
}

void MusicView::finishLoad() {
    if (!startPending || submittingLoads || !pendingLoads.empty()) return;
    startPending = false;
    if (requestedIndex >= loadedEntries.size() || loadedEntries[requestedIndex] < 0) {
        this->reset();
        this->deferIdleTeardown();
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < requestedIndex; ++i)
        if (loadedEntries[i] >= 0) ++index;
    // Every append has replied. The synchronous, state-only selection helper
    // establishes non-idle playback, or returns a real failure. No old queued
    // idle notification can retire metadata while appends are outstanding.
    if (MPVCore::instance().playPlaylistIndex(index, loadedEntries[requestedIndex], loadPlaylistGeneration) < 0) {
        this->playList.clear();
        this->reset();
        this->deferIdleTeardown();
    }
}

void MusicView::deferIdleTeardown() {
    if (!subscriptionsActive || callbackLifetime->teardownPending) return;
    callbackLifetime->teardownPending = true;
    const auto generation = callbackLifetime->generation;
    std::weak_ptr<CallbackLifetime> lifetime = callbackLifetime;
    // Borealis subscriptions are list iterators: never erase one from its own
    // dispatch traversal. A deferred STOP may belong to an older playlist.
    try {
        brls::sync([this, lifetime, generation]() {
            const auto state = lifetime.lock();
            if (!state || !state->active || state->generation != generation) return;
            state->teardownPending = false;
            if (!subscriptionsActive || startPending || this->getParent()) return;
            auto& mpv = MPVCore::instance();
            // END_FILE/STOP also accompanies playlist-next/current-entry. Only the
            // actual idle property confirms that playback has finished completely.
            if (!mpv.isStopped() || mpv.getString("idle-active") != "yes") return;
            this->reset();
            this->unregisterMpvEvent();
        });
    } catch (...) {
        // Failed queue allocation must not permanently latch deferred cleanup.
        callbackLifetime->teardownPending = false;
    }
}
#else
MusicView::~MusicView() { brls::Logger::debug("MusicView: delete"); }
#endif

void MusicView::registerMpvEvent() {
#ifdef PS5_NATIVE_GPU
    if (subscriptionsActive) return;
#endif
    auto& mpv = MPVCore::instance();
    // 生成播放 ID
    const auto ts = std::chrono::system_clock::now().time_since_epoch();
    this->playSession = std::chrono::duration_cast<std::chrono::milliseconds>(ts).count();
    // 注册播放事件回调
#ifdef PS5_NATIVE_GPU
    bool eventRegistered = false, replyRegistered = false;
    try {
        this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
            auto& mpv = MPVCore::instance();
            this->handleReport(event);
            switch (event) {
            case MpvEventEnum::START_FILE:
                if (const auto* track = this->playingTrack()) {
                    this->playTitle->setText(track->Title);
                    this->itemId = track->Id;
                    this->displayEntry = mpv.playbackEvent().entry;
                    // Existing notification is synchronous; reporting above owns
                    // its strings and never retains this borrowed event payload.
                    mpv.getCustomEvent()->fire(TRACK_START, const_cast<Track*>(track));
#else
    this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::START_FILE:
            if (playList.size() > 0) {
                std::string key = fmt::format("playlist/{}/id", mpv.getInt("playlist-playing-pos"));
                auto it = playList.find(mpv.getInt(key));
                if (it != playList.end()) {
                    this->playTitle->setText(it->second.Title);
                    this->itemId = it->second.Id;
                    mpv.getCustomEvent()->fire(TRACK_START, &it->second);
#endif
                }
#ifdef PS5_NATIVE_GPU
                break;
            case MpvEventEnum::MPV_RESUME:
                this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
                break;
            case MpvEventEnum::MPV_PAUSE:
                this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
                break;
            case MpvEventEnum::UPDATE_DURATION:
            case MpvEventEnum::UPDATE_PROGRESS:
                this->updateProgress();
                break;
            case MpvEventEnum::END_OF_FILE:
            case MpvEventEnum::MPV_STOP:
            case MpvEventEnum::MPV_FILE_ERROR:
                if (playList.empty() || displayEntry == mpv.playbackEvent().entry) this->reset();
                [[fallthrough]];
            case MpvEventEnum::MPV_IDLE:
                this->deferIdleTeardown();
                break;
            default:;
#endif
            }
#ifdef PS5_NATIVE_GPU
        });
        eventRegistered = true;
        // 注冊命令回調
        replySubscribeID = mpv.getCommandReply()->subscribe([this](uint64_t userdata, int64_t entryId) {
            auto load = pendingLoads.find(userdata);
            if (load == pendingLoads.end()) return;
            loadedEntries[load->second] = entryId;
            pendingLoads.erase(load);
            auto item = pendingTracks.find(userdata);
            if (item != pendingTracks.end()) {
                if (entryId >= 0) playList.insert_or_assign(entryId, std::move(item->second));
                pendingTracks.erase(item);
            }
            this->finishLoad();
        });
        replyRegistered = true;
        exitSubscribeID = brls::Application::getExitEvent()->subscribe([this] { this->stopReport(); });
    } catch (...) {
        if (replyRegistered) mpv.getCommandReply()->unsubscribe(replySubscribeID);
        if (eventRegistered) mpv.getEvent()->unsubscribe(eventSubscribeID);
        this->playSession = 0;
        throw;
    }
    subscriptionsActive = true;
#else
            break;
        case MpvEventEnum::MPV_RESUME:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->leftStatusLabel->setText(misc::sec2Time(0));
            this->rightStatusLabel->setText(misc::sec2Time(mpv.duration));
            this->osdSlider->setProgress((float)mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
            this->osdSlider->setProgress((float)mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::END_OF_FILE:
            this->reset();
            break;
        case MpvEventEnum::MPV_STOP:
            this->reset();
            if (!this->getParent()) brls::sync([this]() { this->unregisterMpvEvent(); });
            break;
        default:;
        }
    });
    // 注冊命令回調
    replySubscribeID = mpv.getCommandReply()->subscribe([this](uint64_t userdata, int64_t entryId) {
        auto item = reinterpret_cast<jellyfin::Track*>(userdata);
        if (item) playList.insert(std::make_pair(entryId, item));
    });
#endif

    brls::Logger::info("MusicView: registerMpvEvent {}", this->playSession);
}

void MusicView::unregisterMpvEvent() {
#ifdef PS5_NATIVE_GPU
    this->stopReport();
    playlistReportContext.reset();
    if (!subscriptionsActive) return;
    subscriptionsActive = false;
    ++callbackLifetime->generation;
    callbackLifetime->teardownPending = false;
#endif
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCommandReply()->unsubscribe(replySubscribeID);
#ifdef PS5_NATIVE_GPU
    brls::Application::getExitEvent()->unsubscribe(exitSubscribeID);
    pendingTracks.clear();
    pendingLoads.clear();
    loadedEntries.clear();
    startPending = submittingLoads = false;
#endif

    brls::Logger::info("MusicView: unregisterMpvEvent {}", this->playSession);
    // 清空播放ID
    this->playSession = 0;
}

void MusicView::registerViewAction(brls::View* view) {
    auto& mpv = MPVCore::instance();

    view->registerAction(
        "main/player/toggle"_i18n, brls::BUTTON_Y,
#ifdef PS5_NATIVE_GPU
        [this, &mpv](brls::View* view) {
            if (startPending) return true;
#else
        [&mpv](brls::View* view) {
#endif
            if (mpv.isStopped())
                mpv.command("playlist-play-index", "current");
            else
                mpv.togglePlay();
            return true;
        },
        true);

#ifdef PS5_NATIVE_GPU
    view->registerAction("main/player/prev"_i18n, brls::BUTTON_LB, [this, &mpv](brls::View* view) {
        if (startPending) return true;
#else
    view->registerAction("main/player/prev"_i18n, brls::BUTTON_LB, [&mpv](brls::View* view) {
#endif
        mpv.command("playlist-prev");
        return true;
    });
#ifdef PS5_NATIVE_GPU
    view->registerAction(KeyBind::getLast(), [this, &mpv](brls::View* view) {
        if (startPending) return true;
#else
    view->registerAction(KeyBind::getLast(), [&mpv](brls::View* view) {
#endif
        mpv.command("playlist-prev");
        return true;
    });

#ifdef PS5_NATIVE_GPU
    view->registerAction("main/player/next"_i18n, brls::BUTTON_RB, [this, &mpv](brls::View* view) {
        if (startPending) return true;
#else
    view->registerAction("main/player/next"_i18n, brls::BUTTON_RB, [&mpv](brls::View* view) {
#endif
        mpv.command("playlist-next");
        return true;
    });
#ifdef PS5_NATIVE_GPU
    view->registerAction(KeyBind::getNext(), [this, &mpv](brls::View* view) {
        if (startPending) return true;
#else
    view->registerAction(KeyBind::getNext(), [&mpv](brls::View* view) {
#endif
        mpv.command("playlist-next");
        return true;
    });
}

const std::string& MusicView::currentId() { return this->itemId; }

void MusicView::image(brls::Image* image) {
    for (auto& it : this->playList) {
        if (it.second.Id == this->itemId) {
            Image::load(image, jellyfin::apiPrimaryImage, it.second.ImageId,
                HTTP::encode_form({
                    {"tag", it.second.ImageTag},
                    {"maxWidth", "240"},
                }));
        }
    }
}

void MusicView::load(const std::vector<jellyfin::Track>& items, size_t index) {
#ifdef PS5_NATIVE_GPU
    if (items.empty() || index >= items.size()) return;
#endif
    auto& conf = AppConfig::instance();
    auto& mpv = MPVCore::instance();
    std::stringstream ssextra;
    ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
    if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";
    ssextra << fmt::format(",http-header-fields='X-Emby-Token: {}'", conf.getToken());
#ifdef PS5_NATIVE_GPU
    // Audio intent belongs to every queued entry, not the global player. mpv
    // restores this per-file option before a later video load.
    ssextra << ",vid=no";
#endif

#ifdef PS5_NATIVE_GPU
    this->beginLoad(items.size(), index);
    playlistReportContext = jellyfin::RequestContext::capture();
#else
    if (!this->playSession) this->registerMpvEvent();
#endif

    mpv.stop();
    mpv.enableVO(false);
    mpv.command("playlist-clear");
    this->playList.clear();
#ifdef PS5_NATIVE_GPU
    this->pendingTracks.clear();
#endif
    this->btnSuffle->setBorderThickness(0);

#ifdef PS5_NATIVE_GPU
    for (size_t position = 0; position < items.size(); ++position) {
        const auto& item = items[position];
        // Zero is reserved for commands without load ownership. IDs are not reset
        // on replacement, so a delayed old reply cannot identify a new track.
        if (!nextCommandId) ++nextCommandId;
        const uint64_t userdata = nextCommandId++;
        pendingLoads.emplace(userdata, position);
        Track track(item);
        track.PlaySessionId = std::to_string(playSession) + "-" + std::to_string(userdata);
        // Jellyfin accepts the item GUID as an explicit primary-source alias.
        // Preserve that same selector/session in every report for this URL.
        const auto query = HTTP::encode_form({{"static", "true"}, {"MediaSourceId", item.Id},
            {"PlaySessionId", track.PlaySessionId}});
        track.StreamUrl = playlistReportContext->server + fmt::format(fmt::runtime(jellyfin::apiAudio), item.Id, query);
        const auto url = track.StreamUrl;
        pendingTracks.emplace(userdata, std::move(track));
        // Rejected submissions produce no asynchronous reply. Retire only our
        // own metadata here, without an inline callback into the loading view.
        if (mpv.setUrl(url, ssextra.str(), "append", userdata) < 0) {
            pendingTracks.erase(userdata);
            pendingLoads.erase(userdata);
        }
#else
    std::string query = HTTP::encode_form({
        {"static", "true"},
        {"PlaySessionId", std::to_string(playSession)},
    });

    for (auto& item : items) {
        uint64_t userdata = reinterpret_cast<uint64_t>(&item);
        std::string url = fmt::format(fmt::runtime(jellyfin::apiAudio), item.Id, query);
        mpv.setUrl(conf.getUrl() + url, ssextra.str(), "append", userdata);
#endif
    }

#ifdef PS5_NATIVE_GPU
    loadPlaylistGeneration = mpv.playlistGeneration();
    submittingLoads = false;
    this->finishLoad();
#else
    mpv.command("playlist-play-index", std::to_string(index).c_str());
#endif
}

void MusicView::load(const std::vector<remote::DirEntry>& items, size_t index, const std::string& extra) {
#ifdef PS5_NATIVE_GPU
    if (items.empty() || index >= items.size()) return;
#endif
    auto& mpv = MPVCore::instance();
#ifdef PS5_NATIVE_GPU
    const std::string musicOptions = extra + (extra.empty() ? "" : ",") + "vid=no";
#endif

#ifdef PS5_NATIVE_GPU
    this->beginLoad(items.size(), index);
#else
    if (!this->playSession) this->registerMpvEvent();
#endif

    mpv.stop();
    mpv.enableVO(false);
    mpv.command("playlist-clear");
    this->playList.clear();
#ifdef PS5_NATIVE_GPU
    this->pendingTracks.clear();
#endif
    this->btnSuffle->setBorderThickness(0);

#ifdef PS5_NATIVE_GPU
    for (size_t position = 0; position < items.size(); ++position) {
        const auto& item = items[position];
        if (!nextCommandId) ++nextCommandId;
        const uint64_t userdata = nextCommandId++;
        pendingLoads.emplace(userdata, position);
        if (mpv.setUrl(item.url(), musicOptions, "append", userdata) < 0)
            pendingLoads.erase(userdata);
#else
    for (auto& item : items) {
        mpv.setUrl(item.url(), extra, "append");
#endif
    }

#ifdef PS5_NATIVE_GPU
    loadPlaylistGeneration = mpv.playlistGeneration();
    submittingLoads = false;
    this->finishLoad();
}

const MusicView::Track* MusicView::playingTrack() const {
    auto& mpv = MPVCore::instance();
    const auto event = mpv.playbackEvent();
    auto item = playList.find(event.entry);
    if (item == playList.end()) return nullptr;
    const auto position = mpv.getInt("playlist-playing-pos", -1);
    if (position < 0 || mpv.getInt("playlist/" + std::to_string(position) + "/id", -1) != event.entry ||
        mpv.getString("path") != item->second.StreamUrl) return nullptr;
    return &item->second;
}

void MusicView::sendMusicReport(std::string_view endpoint, nlohmann::json data, bool stopping) {
    if (!activeReportContext) return;
    auto context = *activeReportContext;
    if (stopping) { context.cancel.reset(); context.ownerCancel.reset(); }
    jellyfin::postPlaybackReport(std::move(context), endpoint, std::move(data));
}

void MusicView::stopReport(bool failed) {
    // End/replacement callbacks can describe an older entry than live mpv
    // properties. Never sample the next owner's position during old cleanup.
    if (auto data = reporting.stop(reportPosition, reportPaused, reportSeekable)) {
        (*data)["Failed"] = failed;
        sendMusicReport(jellyfin::apiPlayStop, std::move(*data), true);
    }
    activeReportContext.reset();
    reportEntry = -1;
    reportUrl.clear();
}

void MusicView::scheduleReportCheck() {
    if (!reporting.active() || callbackLifetime->reportCheckPending) return;
    callbackLifetime->reportCheckPending = true;
    std::weak_ptr<CallbackLifetime> lifetime = callbackLifetime;
    try {
        brls::delay(5000, [this, lifetime] {
            const auto state = lifetime.lock();
            if (!state || !state->active) return;
            state->reportCheckPending = false;
            // Check the current owned report, not the track that scheduled this
            // timer. Keeping its pending flag across loads bounds queued checks.
            if ((activeReportContext && activeReportContext->cancelled()) ||
                (playlistReportContext && playlistReportContext->cancelled())) {
                stopReport();
                playlistReportContext.reset();
            }
            scheduleReportCheck();
        });
    } catch (...) {
        // Event/exit cleanup remains available if UI timer allocation fails.
        callbackLifetime->reportCheckPending = false;
    }
}

void MusicView::handleReport(MpvEventEnum event) {
    auto& mpv = MPVCore::instance();
    const auto playback = mpv.playbackEvent();
    if ((activeReportContext && activeReportContext->cancelled()) ||
        (playlistReportContext && playlistReportContext->cancelled())) {
        stopReport();
        playlistReportContext.reset();
    }
    if (event == MpvEventEnum::RESET) { stopReport(); return; }
    if (event == MpvEventEnum::END_OF_FILE || event == MpvEventEnum::MPV_STOP || event == MpvEventEnum::MPV_FILE_ERROR) {
        if (playback.entry == reportEntry) stopReport(event == MpvEventEnum::MPV_FILE_ERROR);
        return;
    }
    if (event != MpvEventEnum::START_FILE && event != MpvEventEnum::MPV_RESUME &&
        event != MpvEventEnum::MPV_PAUSE && event != MpvEventEnum::UPDATE_PROGRESS) return;
    const auto* track = playingTrack();
    if (!track) {
        if (reporting.active()) {
            const auto position = mpv.getInt("playlist-playing-pos", -1);
            if (position < 0 || mpv.getInt("playlist/" + std::to_string(position) + "/id", -1) != reportEntry ||
                mpv.getString("path") != reportUrl) stopReport();
        }
        return;
    }
    if (event == MpvEventEnum::START_FILE) { stopReport(); return; }
    if (!playlistReportContext || !playback.started || mpv.isStopped()) return;
    if (reporting.active() && reportEntry != playback.entry) stopReport();
    if (!reporting.active() && !playback.restarted) return;
    const double position = mpv.getDouble("time-pos");
    const bool paused = mpv.getString("pause") == "yes";
    const bool seekable = mpv.getString("seekable") == "yes";
    // mpv's playback thread can advance while properties are queried. Reject a
    // sample if the selected entry/path changed during those reads.
    if (playingTrack() != track) { stopReport(); return; }
    if (!reporting.active()) reportPosition = 0;
    if (std::isfinite(position) && position >= 0) reportPosition = position;
    const bool pauseChanged = paused != reportPaused;
    reportPaused = paused;
    reportSeekable = seekable;
    const auto now = std::chrono::steady_clock::now();
    if (!reporting.active()) {
        activeReportContext = *playlistReportContext;
        reportEntry = playback.entry;
        reportUrl = track->StreamUrl;
        reporting.bind(track->Id, track->Id, track->PlaySessionId, "DirectPlay");
        if (auto data = reporting.start(reportPosition, reportPaused, reportSeekable))
            sendMusicReport(jellyfin::apiPlayStart, std::move(*data));
        lastReportAt = now;
    } else if (reportEntry == playback.entry &&
        (playback.restarted || pauseChanged || now - lastReportAt >= std::chrono::seconds(10))) {
        if (auto data = reporting.progress(reportPosition, reportPaused, reportSeekable))
            sendMusicReport(jellyfin::apiPlaying, std::move(*data));
        lastReportAt = now;
    }
    scheduleReportCheck();
}

void MusicView::updateProgress() {
    auto& mpv = MPVCore::instance();
    const double duration = mpv.duration;
    const double position = mpv.playback_time;
    const bool knownDuration = std::isfinite(duration) && duration > 0;
    const bool knownPosition = std::isfinite(position) && position >= 0;
    this->rightStatusLabel->setText(knownDuration
        ? misc::sec2Time(jellyfin::playbackTicks(duration) / jellyfin::PLAYTICKS) : "--:--");
    this->leftStatusLabel->setText(knownPosition
        ? misc::sec2Time(jellyfin::playbackTicks(position) / jellyfin::PLAYTICKS) : "--:--");
    this->osdSlider->setProgress(knownDuration && knownPosition
        ? static_cast<float>(std::clamp(position / duration, 0.0, 1.0)) : 0);
#else
    mpv.command("playlist-play-index", std::to_string(index).c_str());
#endif
}

void MusicView::reset() {
    this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
    this->rightStatusLabel->setText("--:--");
    this->leftStatusLabel->setText("--:--");
    this->osdSlider->setProgress(0);
    this->itemId.clear();
#ifdef PS5_NATIVE_GPU
    this->displayEntry = -1;
#endif
}

bool MusicView::toggleShuffle() {
#ifdef PS5_NATIVE_GPU
    if (startPending) return true;
#endif
    auto& mpv = MPVCore::instance();

    if (this->btnSuffle->getBorderThickness() > 0) {
        mpv.command("playlist-unshuffle");
        this->btnSuffle->setBorderThickness(0);
    } else {
        mpv.command("playlist-shuffle");
        this->btnSuffle->setBorderThickness(2.0f);
    }
    return true;
}

bool MusicView::toggleLoop() {
    auto& mpv = MPVCore::instance();
    switch (this->repeat) {
    case RepeatNone:
        mpv.command("set", "loop-file", "inf");
        this->repeat = RepeatOne;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-repeat-song.svg");
        break;
    case RepeatOne:
        mpv.command("set", "loop-file", "no");
        mpv.command("set", "loop-playlist", "inf");
        this->repeat = RepeatAll;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-repeat-list.svg");
        break;
    default:
        mpv.command("set", "loop-playlist", "no");
        this->repeat = RepeatNone;
        this->btnRepeatIcon->setImageFromSVGRes("icon/ico-playlist.svg");
    }
    return true;
#ifdef PS5_NATIVE_GPU
}
#else
}
#endif
