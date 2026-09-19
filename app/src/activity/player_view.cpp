#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#ifdef PS5_NATIVE_GPU
#include "api/jellyfin_report_queue.hpp"
#endif
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "view/danmaku_core.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include <tinyxml2.h>
#ifdef PS5_NATIVE_GPU
#include <cstdio>
#include "api/ps5_playback_request.hpp"
#include "utils/ps5_playback_profile.hpp"
#include <borealis/platforms/ps5/native_display.hpp>
#endif

using namespace brls::literals;

PlayerView::PlayerView(const jellyfin::Item& item, const uint64_t seekTicks, const std::string& sourceId)
    : itemId(item.Id) {
#ifdef PS5_NATIVE_GPU
    PlayerSetting::resetTrackSelections();
#endif
    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;
    view = new VideoView();
    view->setDimensions(width, height);
    view->setWidthPercentage(100);
    view->setHeightPercentage(100);
    view->setId("video");
    this->setDimensions(width, height);
    this->addView(view);
    view->registerVideoQuality([this](...) { return this->toggleQuality(); });
#ifdef PS5_NATIVE_GPU
    view->registerPlaybackFailure([this]() {
        if (!playbackReady) return false;
        return retryCompatible(reporting.active()
            ? std::optional<uint64_t>(jellyfin::playbackTicks(MPVCore::instance().playback_time)) : std::nullopt);
    });
#endif

    auto& mpv = MPVCore::instance();

    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });

    settingSubscribeID = view->getSettingEvent()->subscribe([this]() {
#ifdef PS5_NATIVE_GPU
        brls::View* setting = new PlayerSetting(&this->stream, this->playMethod == jellyfin::methodDirectPlay);
#else
        brls::View* setting = new PlayerSetting(&this->stream);
#endif
        brls::Application::pushActivity(new brls::Activity(setting));
    });

    eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
#ifdef PS5_NATIVE_GPU
            this->reportStart();
#endif
            this->reportPlay();
            view->getProfile()->init(this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
#ifdef PS5_NATIVE_GPU
            this->reportStart();
#endif
            this->reportPlay(true);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportStart();
            break;
        case MpvEventEnum::MPV_STOP:
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
#ifdef PS5_NATIVE_GPU
            PlayerSetting::loadTracks(this->stream, this->playMethod == jellyfin::methodDirectPlay);

#else
            auto& svr = AppConfig::instance().getUrl();
            const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
            // 移除其他备用链接
            for (auto& s : this->stream.MediaStreams) {
                if (s.Type == jellyfin::streamTypeSubtitle) {
                    if (s.DeliveryUrl.size() > 0 && (s.IsExternal || this->playMethod == jellyfin::methodTranscode)) {
                        std::string url = svr + s.DeliveryUrl;
                        mpv.command("sub-add", url.c_str(), flag, s.DisplayTitle.c_str());
                    }
                }
            }
            if (PlayerSetting::selectedSubtitle > 0 && this->playMethod == jellyfin::methodDirectPlay) {
                mpv.setInt("sid", PlayerSetting::selectedSubtitle);
            }
#endif
            if (DanmakuCore::PLUGIN_ACTIVE && !this->stream.IsInfiniteStream) {
                this->requestDanmaku();
            }
            break;
        }
        case MpvEventEnum::UPDATE_PROGRESS:
            if (mpv.video_progress % 10 == 0) this->reportPlay();
            break;
        default:;
        }
    });
    // 自定义的mpv事件
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == QUALITY_CHANGE) {
            this->playMedia(MPVCore::instance().playback_time * jellyfin::PLAYTICKS);
        } else if (event == SYNC_STOP) {
            VideoView::close();
        } else if (event == "PreviousTrack") {
            this->view->playNext(-1);
        } else if (event == "NextTrack") {
            this->view->playNext(1);
        }
    });

    if (item.Type != jellyfin::mediaTypeTvChannel) {
        this->sourceId = sourceId.empty() ? item.Id : sourceId;
    }

    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(seekTicks > 0 ? seekTicks : item.UserData.PlaybackPositionTicks);

    // Report stop when application exit
    this->exitSubscribeID = brls::Application::getExitEvent()->subscribe([this]() {
        if (!MPVCore::instance().isStopped()) this->reportStop();
    });
}

PlayerView::~PlayerView() {
#ifdef PS5_NATIVE_GPU
    playbackLifetime->active = false;
    failureIntent.reset();
    view->registerPlaybackFailure({});
#endif
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCustomEvent()->unsubscribe(customEventSubscribeID);
    view->getPlayEvent()->unsubscribe(playSubscribeID);
    view->getSettingEvent()->unsubscribe(settingSubscribeID);

    brls::sync([&mpv]() { mpv.getCustomEvent()->fire(VIDEO_CLOSE, nullptr); });

    if (DanmakuCore::PLUGIN_ACTIVE) {
        DanmakuCore::instance().reset();
    }

#ifdef PS5_NATIVE_GPU
    this->reportStop();
    PlayerSetting::resetTrackSelections();
#else
    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    if (!mpv.isStopped()) this->reportStop();
#endif
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
    brls::Logger::debug("trying delete PlayerView...");
}

void PlayerView::setSeries(const std::string& seriesId) {
    std::string query = HTTP::encode_form({
        {"isVirtualUnaired", "false"},
        {"isMissing", "false"},
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "Chapters"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            int index = -1;
            std::vector<std::string> values;
            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& item = r.Items.at(i);
                if (item.Id == this->itemId) index = i;
                values.push_back(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
            }
            view->setList(values, index);
            this->episodes = std::move(r.Items);
        },
        [ASYNC_TOKEN](const std::string& error) {
            ASYNC_RELEASE
            Dialog::show(error);
        },
        jellyfin::apiShowEpisodes, seriesId, query);
}

void PlayerView::setTitie(const std::string& title) { this->view->setTitie(title); }

void PlayerView::setChapters(const std::vector<jellyfin::MediaChapter>& chaps, uint64_t duration) {
    this->view->setChapters(chaps, duration);
}

bool PlayerView::playIndex(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) {
        return VideoView::close();
    }
#ifdef PS5_NATIVE_GPU
    this->reportStop(); // Capture the old item's position before reset zeroes it.
#endif
    MPVCore::instance().reset();
#ifdef PS5_NATIVE_GPU

    // Track identities belong to this item, never to the next episode.
    // Clear both mpv IDs and server stream selection intent.
    PlayerSetting::resetTrackSelections();
#endif

    auto item = this->episodes.at(index);
    this->itemId = item.Id;
    this->sourceId = item.Id;
    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(0);
    view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
    return true;
}

#ifdef PS5_NATIVE_GPU
void PlayerView::playMedia(const uint64_t seekTicks, bool compatible) {
    this->reportStop();
    playbackReady = false;
    failureIntent.reset();
    const auto requestGeneration = ++playbackLifetime->generation;
    if (!compatible) fallback.reset(seekTicks);
#if defined(__PS4__)
    int maxAllowedHeight = 1080;
#else
    // The explicit backend policy below supplies the complete PS5 envelope.
    int maxAllowedHeight = 1080;
#endif
#else
void PlayerView::playMedia(const uint64_t seekTicks) {
#if defined(__PS4__)
    int maxAllowedHeight = 1080;
#elif defined(__PSV__)
    int maxAllowedHeight = 720;
#else
    int maxAllowedHeight = brls::Application::windowHeight;
    if (MPVCore::VIDEO_QUALITY <= 0) {
    } else if (MPVCore::VIDEO_QUALITY <= 420000) {
        maxAllowedHeight = 360;
    } else if (MPVCore::VIDEO_QUALITY <= 720000) {
        maxAllowedHeight = 480;
    } else if (MPVCore::VIDEO_QUALITY <= 4000000) {
        maxAllowedHeight = 720;
    } else if (MPVCore::VIDEO_QUALITY <= 8000000) {
        maxAllowedHeight = 1080;
    } else if (MPVCore::VIDEO_QUALITY <= 15000000) {
        maxAllowedHeight = 1440;
    } else if (MPVCore::VIDEO_QUALITY <= 120000000) {
        maxAllowedHeight = 2160;
    }
#endif
#endif
    nlohmann::json conditions = {
#if defined(__PSV__)
        {
            {"Condition", "EqualsAny"},
            {"Property", "VideoProfile"},
            {"Value", "high|main|baseline"},
            {"IsRequired", false},
        },
        {
            {"Condition", "LessThanEqual"},
            {"Property", "VideoLevel"},
            {"Value", 40},
            {"IsRequired", false},
        },
#endif
        {
            {"Condition", "LessThanEqual"},
            {"Property", "Height"},
            {"Value", maxAllowedHeight},
            {"IsRequired", false},
        },
    };

    nlohmann::json profile = {
        {"MaxStreamingBitrate", (MPVCore::VIDEO_QUALITY <= 0) ? 120000000 : MPVCore::VIDEO_QUALITY},
        {
            "DirectPlayProfiles",
            {
                {
                    {"Type", "Audio"},
#if defined(__PSV__)
                    {"AudioCodec", "aac,mp3"},
#endif
                },
                {
                    {"Type", "Video"},
#ifdef PS5_NATIVE_GPU
#ifdef __SWITCH__
                    {"VideoCodec", "h264,hevc,av1,vp9"},
#elif defined(__PSV__)
                    {"AudioCodec", "aac,mp3"},
                    {"VideoCodec", "h264"},
#else
                    {"VideoCodec", "h264,hevc"},
#endif
#else
#ifdef __SWITCH__
                    {"VideoCodec", "h264,hevc,av1,vp9"},
#elif defined(__PSV__)
                    {"AudioCodec", "aac,mp3"},
                    {"VideoCodec", "h264"},
#endif
#endif
                },
            },
        },
#ifdef PS5_NATIVE_GPU
#if defined(__PSV__)
        {
            "CodecProfiles",
            {
                {
                    {"Type", "Video"},
                    {"Codec", "h264"},
                    {"Conditions", conditions},
                },
            },
        },
#else
        // Without this the Height condition above only constrains transcoding,
        // and a 2160p source direct-plays straight into six threads of software
        // libavcodec. Applying it as a codec profile makes it bind direct play
        // too, so oversized sources come down already scaled.
        {
            "CodecProfiles",
            {
                {
                    {"Type", "Video"},
                    {"Codec", "h264"},
                    {"Conditions", conditions},
                },
                {
                    {"Type", "Video"},
                    {"Codec", "hevc"},
                    {"Conditions", conditions},
                },
            },
        },
#endif
#else
#if defined(__PSV__)
        {
            "CodecProfiles",
            {
                {
                    {"Type", "Video"},
                    {"Codec", "h264"},
                    {"Conditions", conditions},
                },
            },
        },
#endif
#endif
        {
            "TranscodingProfiles",
            {
                {{"Type", "Audio"}},
                {
                    {"Container", "ts"},
                    {"Type", "Video"},
#if defined(__PSV__)
                    {"VideoCodec", "h264"},
                    {"AudioCodec", "aac,mp3"},
#else
#ifdef PS5_NATIVE_GPU
                    {"VideoCodec", MPVCore::VIDEO_CODEC},
                    {"AudioCodec", "aac,ac3,mp3"},
#else
                    {"VideoCodec", MPVCore::VIDEO_CODEC + ",mpeg4,mpeg2video"},
                    {"AudioCodec", "aac,mp3,ac3,opus,vorbis"},
#endif
#endif
                    {"Protocol", "hls"},
                    {"Conditions", conditions},
                },
            },
        },
        {
            "SubtitleProfiles",
            {
                {{"Format", "srt"}, {"Method", "External"}},
                {{"Format", "srt"}, {"Method", "Embed"}},
                {{"Format", "ass"}, {"Method", "External"}},
                {{"Format", "ass"}, {"Method", "Embed"}},
                {{"Format", "ssa"}, {"Method", "External"}},
                {{"Format", "ssa"}, {"Method", "Embed"}},
                {{"Format", "sub"}, {"Method", "External"}},
                {{"Format", "sub"}, {"Method", "Embed"}},
                {{"Format", "smi"}, {"Method", "External"}},
                {{"Format", "smi"}, {"Method", "Embed"}},
                {{"Format", "vtt"}, {"Method", "External"}},
                {{"Format", "dvdsub"}, {"Method", "Embed"}},
                {{"Format", "dvbsub"}, {"Method", "Embed"}},
                {{"Format", "pgssub"}, {"Method", "Embed"}},
                {{"Format", "pgs"}, {"Method", "Embed"}},
            },
        },
    };

#ifdef PS5_NATIVE_GPU
    constexpr auto backend = ps5::playback::Backend::Hdr10;
    ps5::playback::applyVideoPolicy(profile, backend, MPVCore::VIDEO_CODEC);
    const auto requestId = playbackRequest.begin();
    const auto requestServer = AppConfig::instance().getUrl();
    const auto requestUser = AppConfig::instance().getUserId();
    const auto requestToken = AppConfig::instance().getToken();
    const auto requestCancellation = AppConfig::instance().requestCancellation();
    const auto requestPlaylistGeneration = MPVCore::instance().playlistGeneration();

    nlohmann::json request = {
        {"UserId", AppConfig::instance().getUserId()},
        {"MediaSourceId", this->sourceId},
        {"AllowAudioStreamCopy", true},
        {"DeviceProfile", profile},
        // Keep the server timeline at zero; mpv applies the absolute start
        // position for both direct files and the seekable HLS playlist.
        {"StartTimeTicks", 0},
    };
    request["AllowVideoStreamCopy"] = true;
    if (compatible) ps5::playback::requestCompatiblePlayback(request);
    // Omit unspecified/unmapped tracks so Jellyfin can use its defaults. Zero
    // is a real stream index; subtitle-off is the explicit server value -1.
    if (auto index = PlayerSetting::audioSelection.requestIndex()) request["AudioStreamIndex"] = *index;
    if (auto index = PlayerSetting::subtitleSelection.requestIndex()) request["SubtitleStreamIndex"] = *index;
#if defined(__PSV__)
    request["AlwaysBurnInSubtitleWhenTranscoding"] = PlayerSetting::subtitleSelection.serverIndex.has_value();
#endif
#else
    brls::Logger::debug("PlaybackInfo Audio:{} Sub:{}", PlayerSetting::selectedAudio, PlayerSetting::selectedSubtitle);
#endif

    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    ps5::playback::postInfo(
        request,
        [ASYNC_TOKEN, seekTicks, requestGeneration, requestPlaylistGeneration,
            requestServer, requestUser, requestToken, requestCancellation
            , requestId, compatible, backend
        ](const jellyfin::PlaybackResult& r) {
#else
    jellyfin::postJSON(
        {
            {"UserId", AppConfig::instance().getUserId()},
            {"MediaSourceId", this->sourceId},
            {"AudioStreamIndex", PlayerSetting::selectedAudio},
            {"SubtitleStreamIndex", PlayerSetting::selectedSubtitle},
#if defined(__PSV__)
            {"AlwaysBurnInSubtitleWhenTranscoding", PlayerSetting::selectedSubtitle > 0},
#endif
            {"AllowAudioStreamCopy", true},
            {"DeviceProfile", profile},
        },
        [ASYNC_TOKEN, seekTicks](const jellyfin::PlaybackResult& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (!playbackRequest.finish(requestId)) return;
            if (playbackLifetime->generation != requestGeneration ||
                (requestCancellation && requestCancellation->load()) ||
                MPVCore::instance().playlistGeneration() != requestPlaylistGeneration ||
                !AppConfig::instance().matchesPlaybackSession(requestServer, requestUser, requestToken)) return;
#endif

            if (r.MediaSources.empty()) {
#ifdef PS5_NATIVE_GPU
                this->deferLoadFailure(r.ErrorCode.empty() ? "No compatible playback source was returned." : r.ErrorCode);
#else
                Dialog::show(r.ErrorCode, []() { VideoView::close(); });
#endif
                return;
            }

            auto& mpv = MPVCore::instance();
            auto& svr = AppConfig::instance().getUrl();
#ifdef PS5_NATIVE_GPU
#else
            this->playSessionId = r.PlaySessionId;
#endif

#ifdef PS5_NATIVE_GPU
            const jellyfin::Source* fallbackSource = nullptr;
#endif
            for (auto& item : r.MediaSources) {
#ifdef PS5_NATIVE_GPU
                // Resolve the primary ItemId alias only if no eligible source
                // loads. Explicit version selectors must retain their identity.
                if (!fallbackSource && !item.Id.empty() &&
                    (item.Id == this->sourceId || this->sourceId == this->itemId)) fallbackSource = &item;
                const auto route = ps5::playback::routeFor(item, MPVCore::FORCE_DIRECTPLAY, compatible);
                if (route == ps5::playback::Route::Unavailable) continue;
                // Initial TranscodingUrl can be a remux while video copying is
                // allowed. Unknown originals must use the explicit compatible
                // request, which disables both copy paths. Its MediaStreams
                // still describe the original, not the converted output.
                // The initial HDR milestone is original bytes via direct play.
                // A TranscodingUrl does not prove video copy or PQ preservation;
                // remux, audio conversion and burned subtitles use the explicit
                // SDR retry until their delivered HDR output is qualified.
                if (!compatible && ps5::playback::admitOriginalDelivery(item, backend,
                    route == ps5::playback::Route::Direct) != ps5::playback::Admission::WithinPolicy) continue;
                std::optional<std::string> compatibleUrl;
                if (compatible && route == ps5::playback::Route::Converted) {
                    compatibleUrl = ps5::playback::compatibleMediaUrl(svr, item.TranscodingUrl, item, backend);
                    if (!compatibleUrl) continue;
                }
#endif
                std::stringstream ssextra;
#ifdef _DEBUG
                for (auto& s : item.MediaStreams) {
                    brls::Logger::info("Track {} type {} => {}", s.Index, s.Type, s.DisplayTitle);
                }
#endif
                ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
#ifdef PS5_NATIVE_GPU
                ssextra << PlayerSetting::trackLoadOptions(item);
                if (seekTicks > 0) ssextra << ",start=" << fmt::format("{:.7f}", double(seekTicks) / jellyfin::PLAYTICKS);
#else
                if (seekTicks > 0) ssextra << ",start=" << misc::sec2Time(seekTicks / jellyfin::PLAYTICKS);
#endif
                if (item.IsInfiniteStream) view->hideVideoProgressSlider();

#ifdef PS5_NATIVE_GPU
                auto load = [&](const std::string& url, const std::string& method) {
                    // A rejected submission can retry immediately. Its request
                    // needs the actual source ID for server track selection.
                    if (!item.Id.empty()) this->sourceId = item.Id;
                    // Converted output is bounded to 1080p by the transcoding
                    // profile; only original bytes carry the source geometry.
                    const auto geometry = ps5::playback::originalVideoGeometry(item,
                        route == ps5::playback::Route::Converted);
                    const int result = mpv.setUrl(url, ssextra.str(), "replace", 0, geometry.first, geometry.second);
                    if (result < 0) {
                        brls::Logger::error("mpv: load submission rejected code={}", result);
                        if (!compatible && retryCompatible(seekTicks)) return;
                        this->deferLoadFailure("main/player/error"_i18n);
                        return;
                    }
                    this->playMethod = method;
                    this->stream = item;
                    this->sourceId = item.Id;
                    this->playSessionId = r.PlaySessionId;
                    this->reportContext = jellyfin::RequestContext::capture();
                    this->reporting.bind(this->itemId, item.Id, r.PlaySessionId, method);
                    playbackReady = true;
                };

                if (route == ps5::playback::Route::Remote) {
                    load(item.Path, jellyfin::methodDirectPlay);
#else
                if (item.IsRemote && MPVCore::FORCE_DIRECTPLAY) {
                    mpv.setUrl(item.Path, ssextra.str());
                    this->stream = std::move(item);
#endif
                    return;
                }

                if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";

#ifdef PS5_NATIVE_GPU
                if (route == ps5::playback::Route::Direct) {
#else
                if (item.SupportsDirectPlay || MPVCore::FORCE_DIRECTPLAY) {
#endif
                    std::string url = fmt::format(fmt::runtime(jellyfin::apiStream), this->itemId,
                        HTTP::encode_form({
                            {"static", "true"},
                            {"mediaSourceId", item.Id},
                            {"playSessionId", r.PlaySessionId},
                            {"tag", item.ETag},
                        }));
#ifdef PS5_NATIVE_GPU
                    load(svr + url, jellyfin::methodDirectPlay);
#else
                    this->playMethod = jellyfin::methodDirectPlay;
                    mpv.setUrl(svr + url, ssextra.str());
                    this->stream = std::move(item);
#endif
                    return;
                }

#ifdef PS5_NATIVE_GPU
                if (route == ps5::playback::Route::Converted) {
                    if (compatible) {
                        load(*compatibleUrl, jellyfin::methodTranscode);
                    } else {
                        load(ps5::playback::mediaUrl(svr, item.TranscodingUrl), jellyfin::methodTranscode);
                    }
#else
                if (item.SupportsTranscoding) {
                    this->playMethod = jellyfin::methodTranscode;
                    mpv.setUrl(svr + item.TranscodingUrl, ssextra.str());
                    this->stream = std::move(item);
#endif
                    return;
                }
            }

#ifdef PS5_NATIVE_GPU
            if (!compatible && fallbackSource) {
                this->sourceId = fallbackSource->Id;
                // Do not copy a different version's scoped stream indexes into
                // the compatible request; global subtitle-off still persists.
                PlayerSetting::audioSelection.beginLoad(fallbackSource->Id, fallbackSource->ETag);
                PlayerSetting::subtitleSelection.beginLoad(fallbackSource->Id, fallbackSource->ETag);
            }
            if (!compatible && retryCompatible(seekTicks)) return;
            this->deferLoadFailure("No compatible playback source was returned.");
#else
            VideoView::close();
#endif
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, requestGeneration, requestPlaylistGeneration, requestServer, requestUser, requestToken, requestCancellation
            , requestId
        ](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (!playbackRequest.finish(requestId)) return;
            if (playbackLifetime->generation != requestGeneration ||
                (requestCancellation && requestCancellation->load()) ||
                MPVCore::instance().playlistGeneration() != requestPlaylistGeneration ||
                !AppConfig::instance().matchesPlaybackSession(requestServer, requestUser, requestToken)) return;
            this->deferLoadFailure(ex);
#else
            Dialog::show(ex, []() { VideoView::close(); });
#endif
        },
        jellyfin::apiPlayback, this->itemId);
}

#ifdef PS5_NATIVE_GPU
void PlayerView::frame(brls::FrameContext* ctx) {
    brls::Box::frame(ctx);
    pumpLoadFailure();
}

void PlayerView::deferLoadFailure(const std::string& message) {
    if (failureIntent) return;
    const auto generation = playbackLifetime->generation;
    const auto playlistGeneration = MPVCore::instance().playlistGeneration();
    const auto server = AppConfig::instance().getUrl();
    const auto user = AppConfig::instance().getUserId();
    const auto token = AppConfig::instance().getToken();
    const auto cancellation = AppConfig::instance().requestCancellation();
    std::weak_ptr<PlaybackLifetime> owner = playbackLifetime;
    auto valid = [owner, generation, playlistGeneration, server, user, token, cancellation]() {
        const auto state = owner.lock();
        return state && state->active && state->generation == generation &&
            !(cancellation && cancellation->load()) &&
            MPVCore::instance().playlistGeneration() == playlistGeneration &&
            AppConfig::instance().matchesPlaybackSession(server, user, token);
    };
    failureIntent = std::make_shared<FailureIntent>(FailureIntent{message, std::move(valid)});
    pumpLoadFailure();
}

void PlayerView::pumpLoadFailure() {
    const auto intent = failureIntent;
    if (!intent || intent->queued) return;
    std::weak_ptr<PlaybackLifetime> owner = playbackLifetime;
    intent->queued = true;
    try {
        brls::sync([this, owner, intent]() {
            intent->queued = false; // This ticket can never clear a newer intent's ticket.
            const auto state = owner.lock();
            if (!state || !state->active || failureIntent != intent) return;
            if (!intent->valid()) { failureIntent.reset(); return; }
            const auto activities = brls::Application::getActivitiesStack();
            if (activities.empty() || activities.back()->getContentView() != this) return;
            failureIntent.reset(); // Consume before opening the owned dialog.
            try {
                Dialog::show(intent->message, [this, owner, intent]() {
                    const auto state = owner.lock();
                    if (!state || !state->active || !intent->valid()) return;
                    const auto activities = brls::Application::getActivitiesStack();
                    if (!activities.empty() && activities.back()->getContentView() == this) VideoView::close(true);
                });
            } catch (...) {}
        });
    } catch (...) {
        intent->queued = false; // Keep the original failure for a later frame.
    }
}

void PlayerView::sendReport(std::string_view endpoint, nlohmann::json data, bool stopping) {
    if (!reportContext) return;
    auto context = *reportContext;
    // Cleanup belongs to the old authenticated session even after an account
    // change. Its captured URL/headers must never be replaced by the new account.
    if (stopping) { context.cancel.reset(); context.ownerCancel.reset(); }
    if (auto index = PlayerSetting::audioSelection.requestIndex()) data["AudioStreamIndex"] = *index;
    if (auto index = PlayerSetting::subtitleSelection.requestIndex()) data["SubtitleStreamIndex"] = *index;
    jellyfin::postPlaybackReport(std::move(context), endpoint, std::move(data));
}

#endif
void PlayerView::reportStart() {
#ifdef PS5_NATIVE_GPU
    auto& mpv = MPVCore::instance();
    if (!playbackReady || mpv.isStopped()) return;
    if (auto data = reporting.start(mpv.playback_time, mpv.isPaused(), mpv.canSeek())) {
        if (MPVCore::VIDEO_QUALITY > 0) (*data)["MaxStreamingBitrate"] = MPVCore::VIDEO_QUALITY;
        sendReport(jellyfin::apiPlayStart, std::move(*data));
    }
#else
    uint64_t ticks = MPVCore::instance().playback_time * jellyfin::PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"PositionTicks", ticks},
            {"MediaSourceId", this->stream.Id},
            {"MaxStreamingBitrate", MPVCore::VIDEO_QUALITY},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
#endif
}

void PlayerView::reportStop() {
#ifdef PS5_NATIVE_GPU
    auto& mpv = MPVCore::instance();
    if (auto data = reporting.stop(mpv.playback_time, mpv.isPaused(), mpv.canSeek())) {
        sendReport(jellyfin::apiPlayStop, std::move(*data), true);
        this->playSessionId.clear();
    }
#else
    uint64_t ticks = MPVCore::instance().playback_time * jellyfin::PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStop);

    brls::Logger::debug("PlayerView reportStop {}", this->playSessionId);
    this->playSessionId.clear();
#endif
}

#ifdef PS5_NATIVE_GPU
void PlayerView::reportPlay(std::optional<bool> isPaused) {
    auto& mpv = MPVCore::instance();
    if (auto data = reporting.progress(mpv.playback_time, isPaused.value_or(mpv.isPaused()), mpv.canSeek()))
        sendReport(jellyfin::apiPlaying, std::move(*data));
}

bool PlayerView::retryCompatible(std::optional<uint64_t> ticks) {
    if (!fallback.begin(ticks)) return false;
    playMedia(fallback.position(), true);
    return true;
#else
void PlayerView::reportPlay(bool isPaused) {
    uint64_t ticks = MPVCore::instance().video_progress * jellyfin::PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"MediaSourceId", this->stream.Id},
            {"IsPaused", isPaused},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlaying);
#endif
}

/// 获取视频弹幕
void PlayerView::requestDanmaku() {
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"X-Emby-Token: " + c.getToken()};
        std::string url = fmt::format(fmt::runtime(jellyfin::apiDanmuku), this->itemId);

        try {
            std::string resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{});

            ASYNC_RELEASE
            brls::Logger::debug("DANMAKU: start decode");

            // Load XML
            tinyxml2::XMLDocument document = tinyxml2::XMLDocument();
            tinyxml2::XMLError error = document.Parse(resp.c_str());

            if (error != tinyxml2::XMLError::XML_SUCCESS) {
                brls::Logger::error("Parse danmaku xml[1]: {}", std::to_string(error));
                return;
            }
            tinyxml2::XMLElement* element = document.RootElement();
            if (!element) {
                brls::Logger::error("Decode danmaku xml[2]: no root element");
                return;
            }

            std::vector<DanmakuItem> items;
            for (auto child = element->FirstChildElement(); child != nullptr; child = child->NextSiblingElement()) {
                if (strcmp(child->Name(), "d")) continue;  // 简易判断是不是弹幕
                const char* content = child->GetText();
                if (!content) continue;
                try {
                    items.emplace_back(content, child->Attribute("p"));
                } catch (...) {
                    brls::Logger::error("DANMAKU: error decode: {}", child->GetText());
                }
            }

            brls::sync([items, this]() {
                DanmakuCore::instance().loadDanmakuData(items);
                view->setDanmakuEnable(brls::Visibility::VISIBLE);
            });

            brls::Logger::debug("DANMAKU: decode done: {}", items.size());

        } catch (const std::exception& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("request danmu: {}", ex.what());

            brls::sync([this]() {
                DanmakuCore::instance().reset();
                view->setDanmakuEnable(brls::Visibility::GONE);
            });
        }
    });
}

bool PlayerView::toggleQuality() {
    static std::set<std::string> codecs = {"hevc", "av1", "vp9"};
    std::vector<std::string> options = {"main/player/auto"_i18n};
    std::vector<int64_t> values = {0};
    int64_t videoBitRate = this->stream.Bitrate;
    if (videoBitRate <= 20000000) {
        for (const auto& stream : this->stream.MediaStreams) {
            if (stream.Type == "Video" && codecs.count(stream.Codec) > 0) {
                videoBitRate = round(videoBitRate * 1.5);
                break;
            }
        }
    }

    if (videoBitRate >= 15000000) options.push_back("20 Mbps"), values.push_back(20000000);
    if (videoBitRate >= 10000000) options.push_back("15 Mbps"), values.push_back(15000000);
    if (videoBitRate >= 8000000) options.push_back("10 Mbps"), values.push_back(10000000);
    if (videoBitRate >= 6000000) options.push_back("8 Mbps"), values.push_back(8000000);
    if (videoBitRate >= 4000000) options.push_back("6 Mbps"), values.push_back(6000000);
    if (videoBitRate >= 3000000) options.push_back("4 Mbps"), values.push_back(4000000);
    if (videoBitRate >= 1500000) options.push_back("3 Mbps"), values.push_back(3000000);
    if (videoBitRate >= 720000) options.push_back("1.5 Mbps"), values.push_back(1500000);
    options.push_back("720 kbps"), values.push_back(720000);
    options.push_back("420 kbps"), values.push_back(420000);

    auto it = std::find(values.begin(), values.end(), MPVCore::VIDEO_QUALITY);
    if (it == values.end()) it = values.begin();

    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/quality"_i18n, options,
        [values](int selected) {
            MPVCore::VIDEO_QUALITY = values[selected];
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            return true;
        },
        std::distance(values.begin(), it));

    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}
