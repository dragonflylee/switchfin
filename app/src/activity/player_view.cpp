#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "view/danmaku_core.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"

PlayerView::PlayerView(const jellyfin::Item& item) : itemId(item.Id) {
    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;
    view = new VideoView();
    view->setDimensions(width, height);
    view->setWidthPercentage(100);
    view->setHeightPercentage(100);
    view->setId("video");
    this->setDimensions(width, height);
    this->addView(view);

    if (item.Type == jellyfin::mediaTypeTvChannel) {
        view->hideVideoProgressSlider();
    }

    auto& mpv = MPVCore::instance();
    // 停止正在播放的音乐
    mpv.stop();
    mpv.reset();

    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });

    settingSubscribeID = view->getSettingEvent()->subscribe([this]() {
        brls::View* setting = new PlayerSetting(&this->stream);
        brls::Application::pushActivity(new brls::Activity(setting));
    });

    eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->reportPlay();
            view->getProfile()->init(this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->reportPlay(true);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportStart();
            break;
        case MpvEventEnum::MPV_STOP:
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
            auto& svr = AppConfig::instance().getUrl();
            const char* flag = MPVCore::SUBS_FALLBACK ? "auto" : "select";
            // 移除其他备用链接
            for (auto& s : this->stream.MediaStreams) {
                if (s.Type == jellyfin::streamTypeSubtitle) {
                    if (s.DeliveryUrl.size() > 0 && (s.IsExternal || this->playMethod == jellyfin::methodTranscode)) {
                        std::string url = svr + s.DeliveryUrl;
                        mpv.command("sub-add", url.c_str(), flag, s.DisplayTitle.c_str());
                    }
                }
            }
            if (PlayerSetting::selectedSubtitle > 0) {
                mpv.setInt("sid", PlayerSetting::selectedSubtitle);
            }
            if (DanmakuCore::PLUGIN_ACTIVE) {
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
        }
    });

    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(item.UserData.PlaybackPositionTicks);

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

    if (DanmakuCore::PLUGIN_ACTIVE) {
        DanmakuCore::instance().reset();
    }

    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    if (!mpv.isStopped()) this->reportStop();
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
    brls::Logger::debug("trying delete PlayerView...");
}

void PlayerView::setSeries(const std::string& seriesId) {
    std::string query = HTTP::encode_form({
        {"isVirtualUnaired", "false"},
        {"isMissing", "false"},
        {"userId", AppConfig::instance().getUser().id},
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
    std::vector<float> clips;
    for (auto& c : chaps) {
        clips.push_back(float(c.StartPositionTicks) / float(duration));
    }
    this->view->setClipPoint(clips);
}

bool PlayerView::playIndex(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) {
        return VideoView::close();
    }
    MPVCore::instance().reset();

    auto item = this->episodes.at(index);
    this->itemId = item.Id;
    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(0);
    view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
    return true;
}

void PlayerView::playMedia(const uint64_t seekTicks) {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"UserId", AppConfig::instance().getUser().id},
            {"AudioStreamIndex", PlayerSetting::selectedAudio},
            {"SubtitleStreamIndex", PlayerSetting::selectedSubtitle},
            {"AllowAudioStreamCopy", true},
            {
                "DeviceProfile",
                {
                    {"MaxStreamingBitrate", MPVCore::VIDEO_QUALITY ? MPVCore::VIDEO_QUALITY << 20 : 1 << 24},
                    {
                        "DirectPlayProfiles",
                        {
                            {{"Type", "Audio"}},
                            {
                                {"Type", "Video"},
#ifdef __SWITCH__
                                {"VideoCodec", "h264,hevc,av1,vp9"},
#endif
                            },
                        },
                    },
                    {
                        "TranscodingProfiles",
                        {
                            {{"Type", "Audio"}},
                            {
                                {"Container", "ts"},
                                {"Type", "Video"},
                                {"VideoCodec", MPVCore::VIDEO_CODEC + ",mpeg4,mpeg2video"},
                                {"AudioCodec", "aac,mp3,ac3,opus,vorbis"},
                                {"Protocol", "hls"},
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
                },
            },
        },
        [ASYNC_TOKEN, seekTicks](const jellyfin::PlaybackResult& r) {
            ASYNC_RELEASE

            if (r.MediaSources.empty()) {
                Dialog::show(r.ErrorCode, []() { VideoView::close(); });
                return;
            }

            auto& mpv = MPVCore::instance();
            auto& svr = AppConfig::instance().getUrl();
            this->playSessionId = r.PlaySessionId;

            for (auto& item : r.MediaSources) {
                std::stringstream ssextra;
#ifdef _DEBUG
                for (auto& s : item.MediaStreams) {
                    brls::Logger::info("Track {} type {} => {}", s.Index, s.Type, s.DisplayTitle);
                }
#endif
                ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
                if (HTTP::PROXY_STATUS) {
                    ssextra << ",http-proxy=\"http://" << HTTP::PROXY_HOST << ":" << HTTP::PROXY_PORT << "\"";
                }
                if (seekTicks > 0) {
                    ssextra << ",start=" << misc::sec2Time(seekTicks / jellyfin::PLAYTICKS);
                }
                if (item.SupportsDirectPlay || MPVCore::FORCE_DIRECTPLAY) {
                    std::string url = fmt::format(fmt::runtime(jellyfin::apiStream), this->itemId,
                        HTTP::encode_form({
                            {"static", "true"},
                            {"mediaSourceId", item.Id},
                            {"playSessionId", r.PlaySessionId},
                            {"tag", item.ETag},
                        }));
                    this->playMethod = jellyfin::methodDirectPlay;
                    mpv.setUrl(svr + url, ssextra.str());
                    this->stream = std::move(item);
                    return;
                }

                if (item.SupportsTranscoding) {
                    this->playMethod = jellyfin::methodTranscode;
                    mpv.setUrl(svr + item.TranscodingUrl, ssextra.str());
                    this->stream = std::move(item);
                    return;
                }
            }

            VideoView::close();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { VideoView::close(); });
        },
        jellyfin::apiPlayback, this->itemId);
}

void PlayerView::reportStart() {
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"MediaSourceId", this->stream.Id},
            {"MaxStreamingBitrate", MPVCore::VIDEO_QUALITY},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
}

void PlayerView::reportStop() {
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
}

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
}

/// 获取视频弹幕
void PlayerView::requestDanmaku() {
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"X-Emby-Token: " + c.getUser().access_token};
        std::string url = fmt::format(fmt::runtime(jellyfin::apiDanmuku), this->itemId);

        try {
            std::string resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{});

            ASYNC_RELEASE
            brls::Logger::debug("DANMAKU: start decode");

            // Load XML
            tinyxml2::XMLDocument document = tinyxml2::XMLDocument();
            tinyxml2::XMLError error = document.Parse(resp.c_str());

            if (error != tinyxml2::XMLError::XML_SUCCESS) {
                brls::Logger::error("Error decode danmaku xml[1]: {}", std::to_string(error));
                return;
            }
            tinyxml2::XMLElement* element = document.RootElement();
            if (!element) {
                brls::Logger::error("Error decode danmaku xml[2]: no root element");
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
