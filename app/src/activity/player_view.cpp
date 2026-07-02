/*
    GMCA — Plex video player.
    Verified pipeline: PLEX_MIGRATION.md §2.7.
    Units: mpv positions in seconds, Plex API in milliseconds,
    transcoder offset in whole seconds.
*/

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"

using namespace brls::literals;

/// "Watched" threshold: default value of the server preference
/// LibraryVideoPlayedThreshold
static const double SCROBBLE_THRESHOLD = 0.90;

PlayerView::PlayerView(const plex::Item& item, const int64_t seekMs, int versionIndex)
    : itemId(item.ratingKey), item(item), preferredVersion(versionIndex) {
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
    // direct-access OSD pickers; &stream lets them switch transcode-side
    // tracks (the Vita default) as well as embedded ones
    view->registerVideoSubtitle([this](...) {
        PlayerSetting::showSubtitleMenu(&this->stream);
        return true;
    });
    view->registerVideoAudio([this](...) {
        PlayerSetting::showAudioMenu(&this->stream);
        return true;
    });

    // stable session identifier (24 characters)
    this->sessionId = misc::randHex(12);

    auto& mpv = MPVCore::instance();

    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });

    settingSubscribeID = view->getSettingEvent()->subscribe([this]() {
        brls::View* setting = new PlayerSetting();
        brls::Application::pushActivity(new brls::Activity(setting));
    });

    eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
            view->getProfile()->init(this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->reportTimeline("paused", int64_t(mpv.video_progress) * 1000);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportTimeline("playing", int64_t(mpv.playback_time) * 1000);
            break;
        case MpvEventEnum::MPV_STOP:
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
            const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
            // External (sidecar) subtitles
            for (auto& part : this->stream.parts) {
                for (auto& s : part.streams) {
                    if (s.streamType != media::streamTypeSubtitle || s.key.empty()) continue;
                    std::string url = AppConfig::instance().backend().subtitleSidecarUrl(s.key);
                    mpv.command("sub-add", url.c_str(), flag, s.displayTitle.c_str());
                }
            }
            break;
        }
        case MpvEventEnum::UPDATE_PROGRESS:
            // report cadence: every 10 s
            if (mpv.video_progress % 10 == 0) {
                this->reportTimeline("playing", int64_t(mpv.video_progress) * 1000);
                this->maybeScrobble(int64_t(mpv.video_progress) * 1000);
            }
            break;
        default:;
        }
    });
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == QUALITY_CHANGE) {
            this->playMedia(int64_t(MPVCore::instance().playback_time) * 1000);
        } else if (event == "PreviousTrack") {
            this->view->playNext(-1);
        } else if (event == "NextTrack") {
            this->view->playNext(1);
        }
    });

    this->playMedia(seekMs > 0 ? seekMs : item.viewOffset);

    // Report stop when application exit
    this->exitSubscribeID = brls::Application::getExitEvent()->subscribe([this]() {
        if (!MPVCore::instance().isStopped()) this->reportStop();
    });
}

PlayerView::~PlayerView() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCustomEvent()->unsubscribe(customEventSubscribeID);
    view->getPlayEvent()->unsubscribe(playSubscribeID);
    view->getSettingEvent()->unsubscribe(settingSubscribeID);

    brls::sync([&mpv]() { mpv.getCustomEvent()->fire(VIDEO_CLOSE, nullptr); });

    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    if (!mpv.isStopped()) this->reportStop();
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
    brls::Logger::debug("trying delete PlayerView...");
}

void PlayerView::setSeries(const std::string& showRatingKey) {
    ASYNC_RETAIN
    // all episodes of the show
    AppConfig::instance().backend().getAllEpisodes(showRatingKey, true,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            int index = -1;
            std::vector<std::string> values;
            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& it = r.Items.at(i);
                if (it.ratingKey == this->itemId) index = i;
                values.push_back(fmt::format("S{}E{} - {}", it.parentIndex, it.index, it.title));
            }
            view->setList(values, index);
            this->episodes = std::move(r.Items);
        },
        [ASYNC_TOKEN](const std::string& error) {
            ASYNC_RELEASE
            Dialog::show(error);
        });
}

void PlayerView::setTitie(const std::string& title) { this->view->setTitie(title); }

void PlayerView::setChapters(const std::vector<plex::Chapter>& chaps, int64_t durationMs) {
    std::vector<float> clips;
    if (durationMs > 0) {
        for (auto& c : chaps) {
            clips.push_back(float(c.startTimeOffset) / float(durationMs));
        }
    }
    this->view->setClipPoint(clips);
}

bool PlayerView::playIndex(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) {
        return VideoView::close();
    }
    MPVCore::instance().reset();

    auto next = this->episodes.at(index);
    this->itemId = next.ratingKey;
    this->item = next;
    this->scrobbled = false;
    this->preferredVersion = -1;  // binge: auto-pick the best source for the new episode
    this->playMedia(0);
    view->setTitie(next.grandparentTitle.empty()
                       ? fmt::format("S{}E{} — {}", next.parentIndex, next.index, next.title)
                       : fmt::format("{} · S{}E{} — {}", next.grandparentTitle, next.parentIndex, next.index,
                             next.title));
    return true;
}

void PlayerView::playMedia(const int64_t seekMs) {
    // Fast path: the caller already resolved the exact source (Stremio source
    // picker passes the fully-resolved item + chosen index). Re-fetching would
    // re-resolve streams and could return a different order/set, silently playing
    // a different release than the one selected — so play the chosen one directly.
    {
        auto accessible = [](const plex::Media& m) {
            for (auto& p : m.parts)
                if (p.accessible && p.exists && !p.key.empty()) return true;
            return false;
        };
        if (this->preferredVersion >= 0 && this->preferredVersion < (int)this->item.media.size() &&
            accessible(this->item.media[this->preferredVersion])) {
            this->stream = this->item.media[this->preferredVersion];
            this->setChapters(this->item.chapters, this->item.duration);
            this->startPlayback(seekMs);
            return;
        }
    }

    ASYNC_RETAIN
    // fresh metadata: Media/Part/Stream + chapters
    AppConfig::instance().backend().getItemDetail(
        this->itemId, true,
        [ASYNC_TOKEN, seekMs](const media::Item& item) {
            ASYNC_RELEASE
            this->item = item;

            // caller-chosen source (Stremio picker) if it still resolves to an
            // accessible file; otherwise the first accessible version.
            const plex::Media* chosen = nullptr;
            auto accessible = [](const plex::Media& m) {
                for (auto& p : m.parts)
                    if (p.accessible && p.exists && !p.key.empty()) return true;
                return false;
            };
            if (this->preferredVersion >= 0 && this->preferredVersion < (int)this->item.media.size() &&
                accessible(this->item.media[this->preferredVersion])) {
                chosen = &this->item.media[this->preferredVersion];
            }
            for (auto& m : this->item.media) {
                if (chosen) break;
                if (accessible(m)) chosen = &m;
            }
            if (!chosen) {
                Dialog::show("main/player/error"_i18n, []() { VideoView::close(); });
                return;
            }
            this->stream = *chosen;
            this->setChapters(this->item.chapters, this->item.duration);
            this->startPlayback(seekMs);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { VideoView::close(); });
        });
}

void PlayerView::startPlayback(const int64_t seekMs) {
    media::PlaybackOptions opts;
    opts.seekMs = seekMs;
    opts.bitrateCap = MPVCore::VIDEO_QUALITY;
    opts.forceDirectPlay = MPVCore::FORCE_DIRECTPLAY;
    opts.audioStreamId = PlayerSetting::selectedAudio;
    opts.subtitleStreamId = PlayerSetting::selectedSubtitle;
    opts.burnSubtitles = PlayerSetting::selectedSubtitle > 0;
    // transcode target codec: kept identical to the former hard-coded value
    // (MPVCore::VIDEO_CODEC was never wired into the Plex transcoder — see
    // MULTI_BACKEND.md §6); revisit when exposing the codec choice per backend
    opts.videoCodec = "h264";
    opts.sessionId = this->sessionId;

    // copies for the worker thread (avoids racing on this->item during a switch)
    media::Item item = this->item;
    media::Media version = this->stream;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, item, version, opts]() {
        try {
            // resolvePlayback runs the transcode decision synchronously and
            // throws on failure; the direct-play fallback is internal
            media::PlaybackSource src = AppConfig::instance().backend().resolvePlayback(item, version, opts);
            brls::sync([ASYNC_TOKEN, src]() {
                ASYNC_RELEASE
                // A backend may report "nothing playable" with an empty url
                // (e.g. Stremio with no direct/debrid stream) instead of throwing
                // across the async/TU boundary; surface it as a player error.
                if (src.url.empty()) {
                    Dialog::show("main/player/error"_i18n, []() { VideoView::close(); });
                    return;
                }
                this->playMethod = src.playMethod;
                MPVCore::instance().setUrl(src.url, src.mpvExtra);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                Dialog::show(msg, []() { VideoView::close(); });
            });
        }
    });
}

void PlayerView::reportTimeline(const std::string& state, int64_t timeMs) {
    media::PlayState st = state == "paused"    ? media::PlayState::Paused
                          : state == "stopped" ? media::PlayState::Stopped
                                               : media::PlayState::Playing;
    AppConfig::instance().backend().reportProgress(this->itemId, st, timeMs, this->item.duration, this->sessionId);
}

void PlayerView::reportStop() {
    int64_t timeMs = int64_t(MPVCore::instance().playback_time) * 1000;
    this->reportTimeline("stopped", timeMs);
    this->maybeScrobble(timeMs);
    brls::Logger::debug("PlayerView reportStop {}", this->sessionId);
}

void PlayerView::maybeScrobble(int64_t timeMs) {
    // state=stopped is NOT enough to mark as watched: explicit scrobble required
    if (this->scrobbled || this->item.duration <= 0) return;
    if (double(timeMs) / double(this->item.duration) < SCROBBLE_THRESHOLD) return;
    this->scrobbled = true;
    AppConfig::instance().backend().markWatched(this->itemId);
}

bool PlayerView::toggleQuality() {
    std::vector<std::string> options = {"main/player/auto"_i18n};
    std::vector<int64_t> values = {0};
    int64_t videoBitRate = this->stream.bitrate * 1000;  // Plex: kbps -> bps

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
            // remember the choice across launches (Vita users had to re-lower
            // it every session otherwise — see config.cpp default)
            AppConfig::instance().setItem(AppConfig::PLAYER_VIDEO_QUALITY, MPVCore::VIDEO_QUALITY);
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            return true;
        },
        std::distance(values.begin(), it));

    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}
