#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "view/player_setting.hpp"
#include "view/video_profile.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/config.hpp"
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <sstream>

using namespace brls::literals;

// The position, in ticks, where playback stopped. 1 tick = 10000 ms
static const time_t PLAYTICKS = 10000000;

static std::string sec2Time(int64_t t) {
    if (t < 3600) {
        return fmt::format("{:%M:%S}", std::chrono::seconds(t));
    }
    return fmt::format("{:%H:%M:%S}", std::chrono::seconds(t));
}

static std::vector<int64_t> maxBitrate = {120000000, 10000000, 8000000, 4000000, 2000000};

VideoView::VideoView(jellyfin::MediaItem& item) : itemId(item.Id) {
    this->inflateFromXMLRes("xml/view/video_view.xml");
    brls::Logger::debug("VideoView: create {} type {}", item.Id, item.Type);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    brls::Box* container = new brls::Box();
    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;

    container->setDimensions(width, height);
    this->setDimensions(width, height);
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
    this->setId("video");
    container->addView(this);
    brls::Application::pushActivity(new brls::Activity(container), brls::TransitionAnimation::NONE);

    this->registerAction(
        "cancel", brls::ControllerButton::BUTTON_B,
        [](brls::View* view) -> bool { return brls::Application::popActivity(brls::TransitionAnimation::NONE); }, true);

    this->registerAction(
        "\uE08F", brls::ControllerButton::BUTTON_LB,
        [this](brls::View* view) -> bool {
            seeking_range -= MPVCore::SEEKING_STEP;
            this->requestSeeking();
            return true;
        },
        false, true);

    this->registerAction(
        "\uE08E", brls::ControllerButton::BUTTON_RB,
        [this](brls::View* view) -> bool {
            seeking_range += MPVCore::SEEKING_STEP;
            this->requestSeeking();
            return true;
        },
        false, true);

    this->registerAction(
        "toggleOSD", brls::ControllerButton::BUTTON_Y,
        [this](brls::View* view) -> bool {
            // 拖拽进度时不要影响显示 OSD
            if (!seeking_range) this->toggleOSD();
            return true;
        },
        true);

    /// 播放器设置按钮
    this->btnSetting->registerClickAction([this](...) { return this->showSetting(); });
    this->btnSetting->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSetting));
    this->registerAction(
        "setting", brls::ControllerButton::BUTTON_X, [this](...) { return this->showSetting(); }, true);

    this->registerMpvEvent();

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        this->showOSD(true);
        MPVCore::instance().command_str("seek {} absolute-percent", progress * 100);
    });

    osdSlider->getProgressEvent().subscribe([this](float progress) { this->showOSD(false); });

    this->addGestureRecognizer(new brls::TapGestureRecognizer(this, [this]() { this->toggleOSD(); }));
    /// 播放/暂停 按钮
    this->btnToggle->registerClickAction([](...) {
        auto& mpv = MPVCore::instance();
        mpv.isPaused() ? mpv.resume() : mpv.pause();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

    /// 播放控制
    this->btnBackward->registerClickAction([this](...) { return this->playNext(-1); });
    this->btnBackward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnBackward));

    this->btnForward->registerClickAction([this](...) { return this->playNext(); });
    this->btnForward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnForward));

    static std::vector<std::string> qualities = {
        "main/player/auto"_i18n,
        "1080P 10Mbps",
        "720P 8Mbps",
        "720P 4Mbps",
        "480P 2Mbps",
    };
    this->videoQualityLabel->setText(qualities[this->selectedQuality]);
    this->btnVideoQuality->registerClickAction([this](...) {
        brls::Dropdown* dropdown = new brls::Dropdown(
            "main/player/quality"_i18n, qualities,
            [this](int selected) {
                selectedQuality = selected;
                this->videoQualityLabel->setText(qualities[selected]);
                this->playMedia(MPVCore::instance().playback_time * PLAYTICKS);
            },
            selectedQuality);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    this->btnVideoQuality->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoQuality));

    /// 视频详情信息
    this->profile = new VideoProfile();
    this->addView(this->profile);
    this->registerAction(
        "profile", brls::ControllerButton::BUTTON_BACK,
        [this](brls::View* view) -> bool {
            bool shown = profile->getVisibility() == brls::Visibility::VISIBLE;
            profile->setVisibility(shown ? brls::Visibility::INVISIBLE : brls::Visibility::VISIBLE);
            return true;
        },
        true);

    this->playMedia(item.UserData.PlaybackPositionTicks);
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    MPVCore::instance().stop();
    this->reportStop();
}

void VideoView::setTitie(const std::string& title) { this->titleLabel->setText(title); }

void VideoView::requestSeeking() {
    auto& mpv = MPVCore::instance();
    if (mpv.duration <= 0) {
        seeking_range = 0;
        return;
    }
    double progress = (mpv.playback_time + seeking_range) / mpv.duration;

    if (progress < 0) {
        progress = 0;
        seeking_range = (int64_t)mpv.playback_time * -1;
    } else if (progress > 1) {
        progress = 1;
        seeking_range = mpv.duration;
    }

    showOSD(false);
    osdSlider->setProgress(progress);
    leftStatusLabel->setText(sec2Time(mpv.duration * progress));

    // 延迟触发跳转进度
    brls::cancelDelay(seeking_iter);
    ASYNC_RETAIN
    seeking_iter = brls::delay(400, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        MPVCore::instance().command_str("seek {}", seeking_range);
        seeking_range = 0;
    });
}

bool VideoView::showSetting() {
    brls::View* setting = new PlayerSetting(this->itemSource);
    brls::Application::pushActivity(new brls::Activity(setting));
    // 手动将焦点赋给设置页面
    brls::sync([setting]() { brls::Application::giveFocus(setting); });
    return true;
}

void VideoView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    auto& mpv = MPVCore::instance();
    if (!mpv.isValid()) return;

    // draw video
    mpv.openglDraw(this->getFrame(), this->getAlpha());

    // draw osd
    time_t current = std::time(nullptr);
    if (current < this->osdLastShowTime) {
        if (!this->isOsdShown) {
            this->isOsdShown = true;
            osdTopBox->setVisibility(brls::Visibility::VISIBLE);
            osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
        }
        osdBottomBox->frame(ctx);
        osdTopBox->frame(ctx);
    } else if (this->isOsdShown) {
        this->isOsdShown = false;
        // 当焦点位于video组件内部重新赋予焦点，用来隐藏屏幕上的高亮框
        if (isChildFocused()) brls::Application::giveFocus(this);
        osdTopBox->setVisibility(brls::Visibility::INVISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::INVISIBLE);
    }

    if (current > this->hintLastShowTime) {
        this->hintBox->setVisibility(brls::Visibility::GONE);
        this->hintLastShowTime = 0;
    }

    // cache info
    osdCenterBox->frame(ctx);

    // draw video profile
    if (profile->getVisibility() == brls::Visibility::VISIBLE) {
        static time_t last = current;
        if (current - last > 2) {
            profile->update();
            last = current;
        }
        profile->frame(ctx);
    }
}

void VideoView::invalidate() { View::invalidate(); }

void VideoView::onLayout() {
    brls::View::onLayout();

    brls::Rect rect = getFrame();
    if (oldRect.getWidth() == -1) this->oldRect = rect;

    if (!(rect == oldRect)) {
        brls::Logger::debug("Video view: {} size: {} / {} scale: {}", fmt::ptr(this), rect.getWidth(), rect.getHeight(),
            brls::Application::windowScale);
        MPVCore::instance().setFrameSize(rect);
    }
    oldRect = rect;
}

void VideoView::onChildFocusGained(View* directChild, View* focusedView) {
    Box::onChildFocusGained(directChild, focusedView);
    // 只有在全屏显示OSD时允许OSD组件获取焦点
    if (this->isOsdShown) {
        // 当弹幕按钮隐藏时不可获取焦点
        if (focusedView->getParent()->getVisibility() == brls::Visibility::GONE) {
            brls::Application::giveFocus(this);
        }
        return;
    }
    brls::Application::giveFocus(this);
}

void VideoView::setSeries(const std::string& seriesId) {
    std::string query = HTTP::encode_form({
        {"isVirtualUnaired", "false"},
        {"isMissing", "false"},
        {"userId", AppConfig::instance().getUser().id},
        {"fields", "Chapters"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            for (size_t i = 0; i < r.Items.size(); i++) {
                if (r.Items[i].Id == this->itemId) {
                    this->itemIndex = i;
                    break;
                }
            }
            this->showEpisodes = std::move(r.Items);
            this->btnBackward->setVisibility(this->itemIndex > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            this->btnForward->setVisibility(
                this->itemIndex + 1 < this->showEpisodes.size() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex);
        },
        jellyfin::apiShowEpisodes, seriesId, query);
}

bool VideoView::playNext(int off) {
    this->itemIndex += off;
    if (this->itemIndex < 0 || this->itemIndex >= this->showEpisodes.size()) {
        return brls::Application::popActivity(brls::TransitionAnimation::NONE);
    }
    MPVCore::instance().stop();

    auto item = this->showEpisodes.at(this->itemIndex);
    this->itemId = item.Id;
    this->playMedia(item.UserData.PlaybackPositionTicks);
    this->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
    this->btnBackward->setVisibility(this->itemIndex > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->btnForward->setVisibility(
        this->itemIndex + 1 < this->showEpisodes.size() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    return true;
}

void VideoView::playMedia(const time_t seekTicks) {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"UserId", AppConfig::instance().getUser().id},
            {
                "DeviceProfile",
                {
                    {"MaxStreamingBitrate", maxBitrate[this->selectedQuality]},
                    {
                        "DirectPlayProfiles",
                        {
                            {
                                {"Container", "mp4,m4v,mkv"},
                                {"Type", "Video"},
#ifdef __SWITCH__
                                {"VideoCodec", "h264"},
#else
                                {"VideoCodec", "h264,hevc,av1,vp9"},
#endif
                                {"AudioCodec", "aac,mp3,ac3,eac3,opus"},
                            },
                            {
                                {"Container", "mov"},
                                {"Type", "Video"},
                                {"VideoCodec", "h264"},
                                {"AudioCodec", "aac,mp3,ac3,eac3,opus"},
                            },
                        },
                    },
                    {
                        "TranscodingProfiles",
                        {
                            {
                                {"Container", "ts"},
                                {"Type", "Video"},
                                {"VideoCodec", "h264"},
                                {"AudioCodec", "aac"},
                                {"Protocol", "hls"},
                                {"Context", "Streaming"},
                                {"BreakOnNonKeyFrames", true},
                            },
                        },
                    },
                    {
                        "SubtitleProfiles",
                        {
                            {{"Format", "ass"}, {"Method", "External"}},
                            {{"Format", "ssa"}, {"Method", "External"}},
                            {{"Format", "srt"}, {"Method", "External"}},
                        },
                    },
                },
            },
        },
        [ASYNC_TOKEN, seekTicks](const jellyfin::PlaybackResult& r) {
            ASYNC_RELEASE

            auto& mpv = MPVCore::instance();
            auto& svr = AppConfig::instance().getUrl();
            this->playSessionId = r.PlaySessionId;

            for (auto& item : r.MediaSources) {
                this->itemSource = std::move(item);
                std::stringstream ssextra;
#ifdef _DEBUG
                brls::Logger::debug("mediaSource {} => {}", this->itemId, nlohmann::json(item).dump(1));
#endif
                ssextra << "network-timeout=20";
                if (seekTicks > 0) {
                    ssextra << ",start=" << sec2Time(seekTicks / PLAYTICKS);
                }

#ifdef _WIN32
                const std::string separator = ";";
#else
                const std::string separator = ":";
#endif
                bool hasSub = false;
                int numSub = 0;
                for (auto& s : itemSource.MediaStreams) {
                    if (s.Type == jellyfin::streamTypeSubtitle) {
                        if (s.DeliveryUrl.size() > 0 && (s.IsExternal || item.TranscodingUrl.size() > 0)) {
                            if (hasSub) {
                                ssextra << separator;
                            } else {
                                ssextra << ",sub-files=";
                                hasSub = true;
                            }
                            ssextra << svr << s.DeliveryUrl;
                        }
                        numSub++;
                    }
                }
                if (numSub > 0) {
                    // restore last selected sid
                    ssextra << ",sid=1";
                }

                if (itemSource.SupportsDirectPlay) {
                    std::string url = fmt::format(fmt::runtime(jellyfin::apiStream), this->itemId,
                        HTTP::encode_form({
                            {"static", "true"},
                            {"mediaSourceId", itemSource.Id},
                            {"playSessionId", r.PlaySessionId},
                            {"tag", itemSource.ETag},
                        }));

                    this->playMethod = "DirectPlay";
                    mpv.setUrl(svr + url, ssextra.str());
                    return;
                }

                if (itemSource.SupportsTranscoding) {
                    this->playMethod = "Transcode";
                    mpv.setUrl(svr + itemSource.TranscodingUrl, ssextra.str());
                    return;
                }
            }

            auto dialog = new brls::Dialog("Unsupport video format");
            dialog->addButton(
                "hints/ok"_i18n, []() { brls::Application::popActivity(brls::TransitionAnimation::NONE); });
            dialog->open();
        },
        nullptr, jellyfin::apiPlayback, this->itemId);
}

void VideoView::reportStart() {
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"MediaSourceId", this->itemSource.Id},
            {"MaxStreamingBitrate", maxBitrate[this->selectedQuality]},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
}

void VideoView::reportStop() {
    time_t ticks = MPVCore::instance().playback_time * PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStop);
}

void VideoView::reportPlay(bool isPaused) {
    time_t ticks = MPVCore::instance().video_progress * PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"MediaSourceId", this->itemSource.Id},
            {"IsPaused", isPaused},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlaying);
}

void VideoView::registerMpvEvent() {
    auto& ev = MPVCore::instance().getEvent();
    this->eventSubscribeID = ev.subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->showOSD(true);
            this->hideLoading();
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            this->reportPlay();
            this->profile->init(itemSource.Name, this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->showOSD(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            this->reportPlay(true);
            break;
        case MpvEventEnum::START_FILE:
            this->showOSD(false);
            break;
        case MpvEventEnum::LOADING_START:
            this->showLoading();
            break;
        case MpvEventEnum::LOADING_END:
            this->hideLoading();
            this->reportStart();
            break;
        case MpvEventEnum::MPV_STOP:
            this->hideLoading();
            this->showOSD(false);
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED:
            if (this->seeking_range == 0) {
                this->leftStatusLabel->setText(sec2Time(mpv.video_progress));
            }
            break;
        case MpvEventEnum::UPDATE_DURATION:
            if (this->seeking_range == 0) {
                this->rightStatusLabel->setText(sec2Time(mpv.duration));
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            if (this->seeking_range == 0) {
                this->leftStatusLabel->setText(sec2Time(mpv.video_progress));
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            if (mpv.video_progress % 10 == 0) this->reportPlay();
            break;
        case MpvEventEnum::VIDEO_SPEED_CHANGE:
            break;
        case MpvEventEnum::END_OF_FILE:
            // 播放结束
            this->showOSD(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            this->playNext();
            break;
        case MpvEventEnum::CACHE_SPEED_CHANGE:
            // 仅当加载圈已经开始转起的情况显示缓存
            if (this->osdCenterBox->getVisibility() != brls::Visibility::GONE) {
                if (this->centerLabel->getVisibility() != brls::Visibility::VISIBLE)
                    this->centerLabel->setVisibility(brls::Visibility::VISIBLE);
                this->centerLabel->setText(MPVCore::instance().getCacheSpeed());
            }
            break;
        }
    });
}

void VideoView::unRegisterMpvEvent() {
    auto& ev = MPVCore::instance().getEvent();
    ev.unsubscribe(eventSubscribeID);
}

// Loading
void VideoView::showLoading() {
    this->centerLabel->setVisibility(brls::Visibility::INVISIBLE);
    this->osdCenterBox->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::hideLoading() { this->osdCenterBox->setVisibility(brls::Visibility::GONE); }

/// OSD
void VideoView::toggleOSD() {
    if (this->isOsdShown) {
        this->hideOSD();
    } else {
        this->showOSD(true);
    }
}

void VideoView::showOSD(bool autoHide) {
    if (autoHide) {
        this->osdLastShowTime = std::time(nullptr) + VideoView::OSD_SHOW_TIME;
        this->osd_state = OSDState::SHOWN;
    } else {
        this->osdLastShowTime = 0xffffffff;
        this->osd_state = OSDState::ALWAYS_ON;
    }
}

void VideoView::hideOSD() {
    this->osdLastShowTime = 0;
    this->osd_state = OSDState::HIDDEN;
}

void VideoView::showHint(const std::string& value) {
    brls::Logger::debug("Video hint: {}", value);
    this->hintLabel->setText(value);
    this->hintBox->setVisibility(brls::Visibility::VISIBLE);
    this->hintLastShowTime = std::time(nullptr) + VideoView::OSD_SHOW_TIME;
    this->showOSD();
}