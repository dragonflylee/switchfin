#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/config.hpp"
#include <fmt/format.h>
#include <fmt/chrono.h>

// The position, in ticks, where playback stopped. 1 tick = 10000 ms
static const time_t playTicks = 10000000;

static std::string sec2Time(int64_t t) {
    if (t < 3600) {
        return fmt::format("{:%M:%S}", std::chrono::seconds(t));
    }
    return fmt::format("{:%H:%M:%S}", std::chrono::seconds(t));
}

VideoView::VideoView(jellyfin::MediaItem& item) : itemId(item.Id), userData(&item.UserData) {
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

    this->doPlaybackInfo();

    this->registerAction(
        "cancel", brls::ControllerButton::BUTTON_B,
        [this](brls::View* view) -> bool {
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        },
        true);

    this->registerAction(
        "toggleOSD", brls::ControllerButton::BUTTON_Y,
        [this](brls::View* view) -> bool {
            // 拖拽进度时不要影响显示 OSD
            // if (is_seeking) return true;
            this->toggleOSD();
            return true;
        },
        true);

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
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(
        this->btnToggle,
        [this]() {
            auto& mpv = MPVCore::instance();
            mpv.isPaused() ? mpv.resume() : mpv.pause();
        },
        brls::TapGestureConfig(false, brls::SOUND_NONE, brls::SOUND_NONE, brls::SOUND_NONE)));

    /// 播放器设置按钮
    this->btnSetting->registerClickAction([this](...) { return this->showSetting(); });
    this->btnSetting->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSetting));
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    MPVCore::instance().stop();
    this->userData->PlaybackPositionTicks = MPVCore::instance().video_progress * playTicks;
    this->reportStop();
}

void VideoView::setTitie(const std::string& title) { this->titleLabel->setText(title); }

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
    time_t current = time(nullptr);
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

    // cache info
    osdCenterBox->frame(ctx);
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

void VideoView::doPlaybackInfo() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"UserId", AppConfig::instance().getUser().id},
            {
                "DeviceProfile",
                {{"SubtitleProfiles",
                    {
                        {{"Format", "ass"}, {"Method", "External"}},
                        {{"Format", "ssa"}, {"Method", "External"}},
                        {{"Format", "srt"}, {"Method", "External"}},
                    }}},
            },
        },
        [ASYNC_TOKEN](const jellyfin::PlaybackResult& r) {
            ASYNC_RELEASE
            if (r.MediaSources.empty()) return;
            this->itemSource = std::move(r.MediaSources.front());

            auto& mpv = MPVCore::instance();
            auto& svr = AppConfig::instance().getUrl();

            std::stringstream ssextra;
            ssextra << "network-timeout=20";
            if (this->userData->PlaybackPositionTicks > 0) {
                ssextra << ",start=" << sec2Time(this->userData->PlaybackPositionTicks / playTicks);
            }

#ifdef _WIN32
            const std::string separator = ";";
#else
            const std::string separator = ":";
#endif
            bool hasSub = false;
            for (auto& s : itemSource.MediaStreams) {
                if (s.Type == jellyfin::streamTypeSubtitle && s.IsExternal) {
                    if (hasSub) {
                        ssextra << separator;
                    } else {
                        ssextra << ",sub-files=";
                        hasSub = true;
                    }
                    ssextra << svr << s.DeliveryUrl;
                }
            }

            std::string url = fmt::format(fmt::runtime(jellyfin::apiStream), this->itemId,
                HTTP::encode_query({
                    {"static", "true"},
                    {"mediaSourceId", this->itemSource.Id},
                    {"playSessionId", r.PlaySessionId},
                    {"tag", this->itemSource.ETag},
                }));
            mpv.setUrl(svr + url, ssextra.str());
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex);
        },
        jellyfin::apiPlayback, this->itemId);
}

void VideoView::reportStart() {
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", "DirectPlay"},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
}

void VideoView::reportStop() {
    time_t ticks = MPVCore::instance().playback_time * playTicks;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", "DirectPlay"},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStop);
}

void VideoView::reportPlay(bool isPaused) {
    time_t ticks = MPVCore::instance().video_progress * playTicks;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", "DirectPlay"},
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
            this->reportPlay();
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
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            break;
        case MpvEventEnum::MPV_STOP:
            this->hideLoading();
            this->showOSD(false);
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED:
            this->reportStart();
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->rightStatusLabel->setText(sec2Time(mpv.duration));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->leftStatusLabel->setText(sec2Time(mpv.video_progress));
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            if (mpv.video_progress % 10 == 0) this->reportPlay();
            break;
        case MpvEventEnum::VIDEO_SPEED_CHANGE:
            break;
        case MpvEventEnum::END_OF_FILE:
            // 播放结束
            this->showOSD(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
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
        this->osdLastShowTime = time(nullptr) + VideoView::OSD_SHOW_TIME;
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