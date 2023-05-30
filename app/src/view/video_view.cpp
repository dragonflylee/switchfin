#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/config.hpp"
#include <fmt/format.h>

VideoView::VideoView(const std::string& id) {
    this->inflateFromXMLRes("xml/view/video_view.xml");
    brls::Logger::debug("VideoView: create {}", id);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
    this->setId("video");
    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    this->doMediaSource(id);

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

    this->registerMpvEvent();

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        this->showOSD(true);
        MPVCore::instance().command_str(fmt::format("seek {} absolute-percent", progress * 100).c_str());
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
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    MPVCore::instance().stop();
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
            this->onOSDStateChanged(true);
        }
        osdTopBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
        osdBottomBox->frame(ctx);
        osdTopBox->frame(ctx);
    } else {
        if (this->isOsdShown) {
            this->isOsdShown = false;
            this->onOSDStateChanged(false);
        }
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

void VideoView::doMediaSource(const std::string& itemId) {
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaEpisode& r) {
            ASYNC_RELEASE
            if (r.MediaSources.empty()) return;

            this->videoTitleLabel->setText(r.Name);
            auto& src = r.MediaSources[0];
            auto& mpv = MPVCore::instance();

            mpv.setUrl(fmt::format(fmt::runtime(jellyfin::apiStream), AppConfig::instance().getServerUrl(), src.Id,
                src.Container,
                HTTP::encode_query({
                    {"Static", "true"},
                    {"mediaSourceId", src.Id},
                    {"deviceId", AppVersion::getDeviceName()},
                    {"api_key", AppConfig::instance().getAccessToken()},
                    {"Tag", src.ETag},
                })));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex);
            this->dismiss();
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), itemId);
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
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->showOSD(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            break;
        case MpvEventEnum::START_FILE:
            this->showOSD(false);
            break;
        case MpvEventEnum::LOADING_START:
            this->showLoading();
            break;
        case MpvEventEnum::LOADING_END:
            this->hideLoading();
            break;
        case MpvEventEnum::MPV_STOP:
            this->hideLoading();
            this->showOSD(false);
            break;
        case MpvEventEnum::MPV_LOADED:
            break;
        case MpvEventEnum::UPDATE_DURATION:
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
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

void VideoView::onOSDStateChanged(bool state) {
    // 当焦点位于video组件内部重新赋予焦点，用来隐藏屏幕上的高亮框
    if (!state && isChildFocused()) {
        brls::Application::giveFocus(this);
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