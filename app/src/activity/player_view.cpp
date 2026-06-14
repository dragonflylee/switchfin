/*
    pleNx — Plex video player.
    Verified pipeline: PLEX_MIGRATION.md §2.7.
    Units: mpv positions in seconds, Plex API in milliseconds,
    transcoder offset in whole seconds.
*/

#include "activity/player_view.hpp"
#include "api/plex.hpp"
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

PlayerView::PlayerView(const plex::Item& item, const int64_t seekMs) : itemId(item.ratingKey), item(item) {
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
            auto& conf = AppConfig::instance();
            const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
            // External (sidecar) subtitles: {base}{Stream.key}?encoding=utf-8
            for (auto& part : this->stream.parts) {
                for (auto& s : part.streams) {
                    if (s.streamType != plex::streamTypeSubtitle || s.key.empty()) continue;
                    std::string url =
                        fmt::format("{}{}?encoding=utf-8&X-Plex-Token={}", conf.getUrl(), s.key, conf.getToken());
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
    std::string query = HTTP::encode_form({{"includeStreams", "1"}});
    auto& conf = AppConfig::instance();

    ASYNC_RETAIN
    // all episodes of the show
    plex::getJSON<plex::Container<plex::Item>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
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
        },
        plex::apiGrandchildren, showRatingKey, query);
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
    this->playMedia(0);
    view->setTitie(next.grandparentTitle.empty()
                       ? fmt::format("S{}E{} — {}", next.parentIndex, next.index, next.title)
                       : fmt::format("{} · S{}E{} — {}", next.grandparentTitle, next.parentIndex, next.index,
                             next.title));
    return true;
}

void PlayerView::playMedia(const int64_t seekMs) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({
        {"includeChapters", "1"},
        {"includeMarkers", "1"},
        {"includeStreams", "1"},
        {"checkFiles", "1"},
    });

    ASYNC_RETAIN
    // fresh metadata: Media/Part/Stream + chapters
    plex::getJSON<plex::Container<plex::Item>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN, seekMs](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                Dialog::show("main/plex/unreachable"_i18n, []() { VideoView::close(); });
                return;
            }
            this->item = r.Items.front();

            // first version whose file is accessible
            const plex::Media* chosen = nullptr;
            for (auto& m : this->item.media) {
                for (auto& p : m.parts) {
                    if (p.accessible && p.exists && !p.key.empty()) {
                        chosen = &m;
                        break;
                    }
                }
                if (chosen) break;
            }
            if (!chosen) {
                Dialog::show("main/player/error"_i18n, []() { VideoView::close(); });
                return;
            }
            this->stream = *chosen;
            this->setChapters(this->item.chapters, this->item.duration);

            if (MPVCore::VIDEO_QUALITY <= 0 || MPVCore::FORCE_DIRECTPLAY) {
                this->playDirect(seekMs);
            } else {
                this->playTranscode(seekMs);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { VideoView::close(); });
        },
        plex::apiMetadata, this->itemId, query);
}

void PlayerView::playDirect(const int64_t seekMs) {
    auto& conf = AppConfig::instance();
    auto& mpv = MPVCore::instance();

    std::stringstream ssextra;
    ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
    if (seekMs > 0) ssextra << ",start=" << misc::sec2Time(seekMs / 1000);
    if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";

    // direct play: {base}{Part.key}?X-Plex-Token=...
    const plex::Part& part = this->stream.parts.front();
    std::string url = plex::withToken(conf.getUrl() + part.key, conf.getToken());
    this->playMethod = "directplay";
    mpv.setUrl(url, ssextra.str());
}

void PlayerView::playTranscode(const int64_t seekMs) {
    auto& conf = AppConfig::instance();
    // transcoder session: regenerated on every start
    this->transcodeSession = misc::randHex(12);

    // Universal transcoder parameters.
    // Documented revision: protocol=hls (PLEX_MIGRATION.md "Révision (phase 4)")
    HTTP::Form form = {
        {"hasMDE", "1"},
        {"path", fmt::format("/library/metadata/{}", this->itemId)},
        {"mediaIndex", "0"},
        {"partIndex", "0"},
        {"protocol", "hls"},
        {"fastSeek", "1"},
        {"directPlay", "0"},
        {"directStream", "1"},
        {"directStreamAudio", "0"},
        {"subtitleSize", "100"},
        {"audioBoost", "100"},
        {"location", "lan"},
        {"addDebugOverlay", "0"},
        {"autoAdjustQuality", "0"},
        {"mediaBufferSize", "102400"},
        {"maxVideoBitrate", std::to_string(MPVCore::VIDEO_QUALITY / 1000)},  // kbps
        {"session", this->transcodeSession},
        {"X-Plex-Session-Identifier", this->sessionId},
        {"X-Plex-Platform", "Generic"},  // mandatory: other values -> HTTP 400
        {"X-Plex-Client-Identifier", conf.getDeviceId()},
        {"X-Plex-Product", AppVersion::getPackageName()},
        {"X-Plex-Version", AppVersion::getVersion()},
        {"X-Plex-Token", conf.getToken()},
    };
    if (seekMs > 0) form["offset"] = std::to_string(seekMs / 1000);  // whole seconds

    // selected tracks: Plex Stream IDs (not mpv indexes)
    if (PlayerSetting::selectedAudio > 0) form["audioStreamID"] = std::to_string(PlayerSetting::selectedAudio);
    if (PlayerSetting::selectedSubtitle > 0) {
        form["subtitleStreamID"] = std::to_string(PlayerSetting::selectedSubtitle);
        form["subtitles"] = "burn";  // burn-in (HLS; cf. phase 4 revision)
    } else {
        form["subtitles"] = "none";
    }

    // Client profile clauses, each percent-encoded then joined with "+"
    std::vector<std::string> clauses = {
        "add-settings(DirectPlayStreamSelection=true)",
        fmt::format("add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.bitrate&value={}&"
                    "replace=true)",
            MPVCore::VIDEO_QUALITY / 1000),
        "add-transcode-target(type=videoProfile&context=streaming&protocol=hls&container=mpegts&videoCodec=h264&"
        "audioCodec=aac,ac3,mp3&replace=true)",
    };
    std::string profile;
    for (auto& clause : clauses) {
        HTTP::Form one = {{"p", clause}};
        std::string encoded = HTTP::encode_form(one).substr(2);  // strips "p="
        profile += (profile.empty() ? "" : "+") + encoded;
    }
    std::string query = HTTP::encode_form(form) + "&X-Plex-Client-Profile-Extra=" + profile;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, query]() {
        auto& conf = AppConfig::instance();
        try {
            // decision: codes >= 2000 = failure, 1000 = direct play only
            std::string url = conf.getUrl() + fmt::format(fmt::runtime(plex::apiTranscodeDecision), query);
            nlohmann::json decision = plex::getSync(url, "", 10000);
            const nlohmann::json& mc =
                decision.contains("MediaContainer") ? decision.at("MediaContainer") : decision;
            int64_t general = plex::jint(mc, "generalDecisionCode");
            int64_t transcode = plex::jint(mc, "transcodeDecisionCode");
            int64_t mde = plex::jint(mc, "mdeDecisionCode");

            brls::sync([ASYNC_TOKEN, query, general, transcode, mde]() {
                ASYNC_RELEASE
                if (general >= 2000 || mde >= 2000) {
                    Dialog::show(fmt::format("{} ({})", "main/player/error"_i18n, general), []() {});
                    return;
                }
                if (transcode == 1000) {
                    // the server refuses to transcode: direct play
                    this->playDirect(0);
                    return;
                }
                auto& conf = AppConfig::instance();
                auto& mpv = MPVCore::instance();
                std::stringstream ssextra;
                ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
                if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";

                std::string play =
                    conf.getUrl() + "/video/:/transcode/universal/start.m3u8?" + query;
                this->playMethod = "transcode";
                mpv.setUrl(play, ssextra.str());
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
    auto& conf = AppConfig::instance();
    HTTP::Form form = {
        {"ratingKey", this->itemId},
        {"key", fmt::format("/library/metadata/{}", this->itemId)},
        {"state", state},
        {"time", std::to_string(timeMs)},
        {"X-Plex-Session-Identifier", this->sessionId},
    };
    if (this->item.duration > 0) form["duration"] = std::to_string(this->item.duration);

    std::string url =
        conf.getUrl() + fmt::format(fmt::runtime(plex::apiTimeline), HTTP::encode_form(form));
    std::string token = conf.getToken();
    brls::async([url, token]() {
        try {
            // POST, parameters in query, empty body
            plex::postSync(url, token);
        } catch (const std::exception& ex) {
            brls::Logger::warning("plex timeline: {}", ex.what());
        }
    });
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

    auto& conf = AppConfig::instance();
    plex::getAction(conf.getUrl(), conf.getToken(), nullptr, plex::apiScrobble, this->itemId);
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
