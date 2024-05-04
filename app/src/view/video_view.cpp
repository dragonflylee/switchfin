#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "view/player_setting.hpp"
#include "view/video_profile.hpp"
#include "view/danmaku_core.hpp"
#include "view/danmaku_setting.hpp"
#include "api/jellyfin.hpp"
#include "utils/gesture.hpp"
#include "utils/dialog.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"

const int VIDEO_SEEK_NODELAY = 0;

using namespace brls::literals;

#define CHECK_OSD(shake)                                                              \
    if (this->isOsdLock) {                                                            \
        if (this->isOsdShown) {                                                       \
            brls::Application::giveFocus(this->osdLockBox);                           \
            if (shake) this->osdLockBox->shakeHighlight(brls::FocusDirection::RIGHT); \
        } else {                                                                      \
            this->showOSD(true);                                                      \
        }                                                                             \
        return true;                                                                  \
    }

VideoView::VideoView(const jellyfin::MediaItem& item) : itemId(item.Id) {
    this->inflateFromXMLRes("xml/view/video_view.xml");
    brls::Logger::debug("VideoView: create with item {}", itemId);
    this->setHideHighlightBorder(true);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    // 停止正在播放的音乐
    MPVCore::instance().stop();
    MPVCore::instance().reset();

    this->input = brls::Application::getPlatform()->getInputManager();

    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;
    brls::Box* container = new brls::Box();
    container->setDimensions(width, height);
    this->setDimensions(width, height);
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
    this->setId("video");
    container->addView(this);
    brls::Application::pushActivity(new brls::Activity(container), brls::TransitionAnimation::NONE);

    this->registerAction(
        "hints/back"_i18n, brls::BUTTON_B,
        [this](brls::View* view) {
            if (isOsdLock) {
                this->toggleOSD();
                return true;
            }
            return dismiss();
        },
        true);

    this->registerAction(
        "\uE08F", brls::BUTTON_LB,
        [this](brls::View* view) -> bool {
            CHECK_OSD(true);
            this->seekingRange -= MPVCore::SEEKING_STEP;
            this->requestSeeking(seekingRange);
            return true;
        },
        false, true);

    this->registerAction(
        "\uE08E", brls::BUTTON_RB,
        [this](brls::View* view) -> bool {
            CHECK_OSD(true);
            this->seekingRange += MPVCore::SEEKING_STEP;
            this->requestSeeking(seekingRange);
            return true;
        },
        false, true);

    this->registerAction(
        "toggleOSD", brls::BUTTON_Y,
        [this](brls::View* view) -> bool {
            // 拖拽进度时不要影响显示 OSD
            if (!this->seekingRange) this->toggleOSD();
            return true;
        },
        true);

    /// 播放器设置按钮
    this->btnSetting->registerClickAction([this](brls::View* view) {
        this->showSetting();
        return true;
    });
    this->btnSetting->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnSetting));
    this->registerAction(
        "main/player/setting"_i18n, brls::BUTTON_X,
        [this](brls::View* view) {
            this->showSetting();
            return true;
        },
        true);

    /// 音量按钮
    this->btnVolume->registerClickAction([this](brls::View* view) { return this->toggleVolume(view); });
    this->btnVolume->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVolume));

    /// 弹幕切换按钮
    this->btnDanmakuIcon->getParent()->registerClickAction([this](...) { return this->toggleDanmaku(); });
    this->btnDanmakuIcon->getParent()->addGestureRecognizer(
        new brls::TapGestureRecognizer(this->btnDanmakuIcon->getParent()));

    /// 弹幕设置按钮
    this->btnDanmakuSettingIcon->getParent()->registerClickAction([](...) {
        auto setting = new DanmakuSetting();
        brls::Application::pushActivity(new brls::Activity(setting));
        return true;
    });
    this->btnDanmakuSettingIcon->getParent()->addGestureRecognizer(
        new brls::TapGestureRecognizer(this->btnDanmakuSettingIcon->getParent()));

    this->registerMpvEvent();

    osdSlider->getProgressSetEvent().subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        this->showOSD(true);
        MPVCore::instance().seek(progress * 100, "absolute-percent");
    });

    osdSlider->getProgressEvent().subscribe([this](float progress) { this->showOSD(false); });

    /// 组件触摸事件
    /// 单击控制 OSD
    /// 双击控制播放与暂停
    /// 长按加速
    /// 滑动调整进度
    /// 左右侧滑动调整音量，在支持调节背光的设备上左侧滑动调节背光亮度，右侧调节音量
    this->addGestureRecognizer(new OsdGestureRecognizer([this](OsdGestureStatus status) {
        auto& mpv = MPVCore::instance();

        switch (status.osdGestureType) {
        case OsdGestureType::TAP:
            this->toggleOSD();
            break;
        case OsdGestureType::DOUBLE_TAP_END:
            if (isOsdLock) {
                this->toggleOSD();
                break;
            }
            mpv.togglePlay();
            break;
        case OsdGestureType::LONG_PRESS_START: {
            if (isOsdLock) break;
            float cur = MPVCore::VIDEO_SPEED == 100 ? 2.0 : MPVCore::VIDEO_SPEED * 0.01f;
            MPVCore::instance().setSpeed(cur);
            // 绘制临时加速标识
            this->speedHintLabel->setText(fmt::format(fmt::runtime("main/player/speed_up"_i18n), cur));
            this->speedHintBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        }
        case OsdGestureType::LONG_PRESS_CANCEL:
        case OsdGestureType::LONG_PRESS_END:
            if (isOsdLock) {
                mpv.togglePlay();
                break;
            }
            mpv.setSpeed(1.0f);
            this->speedHintBox->setVisibility(brls::Visibility::GONE);
            break;
        case OsdGestureType::HORIZONTAL_PAN_START:
            if (isOsdLock) break;
            infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
            osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        case OsdGestureType::HORIZONTAL_PAN_UPDATE:
            if (isOsdLock) break;
            this->requestSeeking(fmin(120.0f, mpv.duration) * status.deltaX);
            break;
        case OsdGestureType::HORIZONTAL_PAN_CANCEL:
            if (isOsdLock) break;
            // 立即取消
            this->requestSeeking(0, VIDEO_SEEK_NODELAY);
            break;
        case OsdGestureType::HORIZONTAL_PAN_END:
            if (isOsdLock) {
                this->toggleOSD();
                break;
            }
            // 立即跳转
            this->requestSeeking(fmin(120.0f, mpv.duration) * status.deltaX, VIDEO_SEEK_NODELAY);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_START:
            if (isOsdLock) break;
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                this->brightnessInit = brls::Application::getPlatform()->getBacklightBrightness();
                infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
                osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_START:
            if (isOsdLock) break;
            this->volumeInit = mpv.volume;
            infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
            osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_UPDATE:
            if (isOsdLock) break;
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                this->requestBrightness(this->brightnessInit + status.deltaY);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_UPDATE:
            if (isOsdLock) break;
            this->requestVolume(this->volumeInit + status.deltaY * 100);
            break;
        case OsdGestureType::LEFT_VERTICAL_PAN_CANCEL:
        case OsdGestureType::LEFT_VERTICAL_PAN_END:
            if (isOsdLock) {
                this->toggleOSD();
                break;
            }
            if (brls::Application::getPlatform()->canSetBacklightBrightness()) {
                osdInfoBox->setVisibility(brls::Visibility::GONE);
                break;
            }
        case OsdGestureType::RIGHT_VERTICAL_PAN_CANCEL:
        case OsdGestureType::RIGHT_VERTICAL_PAN_END:
            if (isOsdLock) {
                this->toggleOSD();
                break;
            }
            osdInfoBox->setVisibility(brls::Visibility::GONE);
            break;
        default:
            break;
        }
    }));

    /// 播放/暂停 按钮
    this->btnToggle->registerClickAction([](...) {
        MPVCore::instance().togglePlay();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

    /// OSD 锁定按钮
    this->osdLockBox->registerClickAction([this](...) { return this->toggleOSDLock(); });
    this->osdLockBox->addGestureRecognizer(new brls::TapGestureRecognizer(this->osdLockBox));

    this->btnClose->registerClickAction([](...) {
        brls::sync([]() { dismiss(); });
        return true;
    });
    this->btnClose->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnClose));

    /// 播放控制
    this->btnBackward->registerClickAction([this](...) { return this->playIndex(this->itemIndex - 1); });
    this->btnBackward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnBackward));

    this->btnForward->registerClickAction([this](...) { return this->playIndex(this->itemIndex + 1); });
    this->btnForward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnForward));

    this->registerAction("main/player/toggle"_i18n, brls::BUTTON_SPACE, [this](brls::View* view) -> bool {
        MPVCore::instance().togglePlay();
        if (MPVCore::OSD_ON_TOGGLE) {
            this->showOSD(true);
        }
        return true;
    });
    this->registerAction("main/player/toggle"_i18n, brls::BUTTON_A, [this](brls::View* view) -> bool {
        MPVCore::instance().togglePlay();
        if (MPVCore::OSD_ON_TOGGLE) {
            this->showOSD(true);
        }
        return true;
    });
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    this->registerAction(
        "", brls::BUTTON_F,
        [](...) {
            VideoContext::FULLSCREEN = !AppConfig::instance().getItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, VideoContext::FULLSCREEN);
            brls::Application::getPlatform()->getVideoContext()->fullScreen(VideoContext::FULLSCREEN);
            return true;
        },
        true);
#endif
    /// 视频质量
    this->btnVideoQuality->registerClickAction([this](brls::View* view) { return this->toggleQuality(); });
    this->btnVideoQuality->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoQuality));
    this->registerAction(
        "main/player/quality"_i18n, brls::BUTTON_RSB, [this](...) { return this->toggleQuality(); }, true);

    /// 视频详情信息
    this->profile = new VideoProfile();
    this->addView(this->profile);
    this->registerAction(
        "profile", brls::BUTTON_BACK, [this](brls::View* view) { return this->toggleProfile(); }, true);
    this->btnCast->registerClickAction([this](...) { return this->toggleProfile(); });
    this->btnCast->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnCast));

    /// 倍速按钮
    this->btnVideoSpeed->registerClickAction([this](...) { return this->toggleSpeed(); });
    this->btnVideoSpeed->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoSpeed));
    this->registerAction(
        "main/player/speed"_i18n, brls::BUTTON_LSB, [this](...) { return this->toggleSpeed(); }, true);

    /// 章节信息
    this->btnVideoChapter->registerClickAction([this](brls::View* view) {
        if (this->videoChapters.empty()) return false;

        int selectedChapter = -1;
        time_t ticks = MPVCore::instance().video_progress * jellyfin::PLAYTICKS;
        std::vector<std::string> values;
        for (auto& item : this->videoChapters) {
            values.push_back(item.Name);
            if (item.StartPositionTicks <= ticks) selectedChapter++;
        }

        brls::Dropdown* dropdown = new brls::Dropdown(
            "main/player/chapter"_i18n, values,
            [this](int selected) {
                int64_t offset = this->videoChapters[selected].StartPositionTicks;
                MPVCore::instance().seek(offset / jellyfin::PLAYTICKS);
            },
            selectedChapter);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    this->btnVideoChapter->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoChapter));

    // 选集
    this->btnEpisode->registerClickAction([this](brls::View* view) {
        if (this->showEpisodes.empty()) return false;

        std::vector<std::string> values;
        for (auto& item : this->showEpisodes) {
            values.push_back(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        }

        brls::Dropdown* dropdown = new brls::Dropdown(
            "main/player/episode"_i18n, values, [this](int selected) { this->playIndex(selected); }, this->itemIndex);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    this->btnEpisode->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnEpisode));

    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(item.UserData.PlaybackPositionTicks);
    // Report stop when application exit
    this->exitSubscribeID = brls::Application::getExitEvent()->subscribe([this]() { this->reportStop(); });
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    disableDimming(false);

    MPVCore::instance().stop();
    if (this->playSessionId.size()) this->reportStop();
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
}

void VideoView::setTitie(const std::string& title) { this->titleLabel->setText(title); }

void VideoView::requestSeeking(int seek, int delay) {
    auto& mpv = MPVCore::instance();
    if (mpv.duration <= 0) {
        this->seekingRange = 0;
        return;
    }
    double progress = (mpv.playback_time + seek) / mpv.duration;

    if (progress < 0) {
        progress = 0;
        seek = (int64_t)mpv.playback_time * -1;
    } else if (progress > 1) {
        progress = 1;
        seek = mpv.duration;
    }

    showOSD(false);
    if (osdInfoBox->getVisibility() != brls::Visibility::VISIBLE) {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-seeking.svg");
    }
    infoLabel->setText(fmt::format("{:+d} s", seek));
    osdSlider->setProgress(progress);
    leftStatusLabel->setText(misc::sec2Time(mpv.duration * progress));

    // 延迟触发跳转进度
    brls::cancelDelay(this->seekingIter);
    if (delay <= VIDEO_SEEK_NODELAY) {
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->seekingRange = 0;
        if (seek == 0) return;
        MPVCore::instance().seek(seek, "relative");
    } else if (delay > 0) {
        ASYNC_RETAIN
        this->seekingIter = brls::delay(delay, [ASYNC_TOKEN, seek]() {
            ASYNC_RELEASE
            osdInfoBox->setVisibility(brls::Visibility::GONE);
            this->seekingRange = 0;
            if (seek == 0) return;
            MPVCore::instance().seek(seek, "relative");
        });
    }
}

void VideoView::requestVolume(int value, int delay) {
    if (value < 0) value = 0;
    if (value > 200) value = 200;
    MPVCore::instance().setInt("volume", value);
    infoLabel->setText(fmt::format("{:+d} %", value));

    if (delay == 0) return;
    if (this->volumeIter == 0) {
        osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
        infoIcon->setImageFromSVGRes("icon/ico-volume.svg");
    } else {
        brls::cancelDelay(this->volumeIter);
    }
    ASYNC_RETAIN
    volumeIter = brls::delay(delay, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        osdInfoBox->setVisibility(brls::Visibility::GONE);
        this->volumeIter = 0;
    });
}

void VideoView::requestBrightness(float value) {
    if (value < 0) value = 0.0f;
    if (value > 1) value = 1.0f;
    brls::Application::getPlatform()->setBacklightBrightness(value);
    infoLabel->setText(fmt::format("{} %", (int)(value * 100)));
    infoIcon->setImageFromSVGRes("icon/ico-sun-fill.svg");
    osdInfoBox->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::showSetting() {
    brls::View* setting = new PlayerSetting(this->itemSource);
    brls::Application::pushActivity(new brls::Activity(setting));
}

void VideoView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    auto& mpv = MPVCore::instance();
    if (!mpv.isValid()) return;

    // draw video
    mpv.draw(this->getFrame(), this->getAlpha());

    if (enableDanmaku) DanmakuCore::instance().draw(vg, x, y, w, h, alpha);

    // draw osd
    brls::Time current = brls::getCPUTimeUsec();
    if ((this->osdState == OSDState::SHOWN && current < this->osdLastShowTime) ||
        this->osdState == OSDState::ALWAYS_ON) {
        // 当 osd 锁定时，只显示锁定按钮
        if (!isOsdLock) {
            if (!this->isOsdShown) {
                this->isOsdShown = true;
                osdTopBox->setVisibility(brls::Visibility::VISIBLE);
                osdBottomBox->setVisibility(brls::Visibility::VISIBLE);
            }
            osdBottomBox->frame(ctx);
            osdTopBox->frame(ctx);
        }

        osdLockBox->setVisibility(brls::Visibility::VISIBLE);
        osdLockBox->frame(ctx);

    } else if (this->isOsdShown) {
        this->isOsdShown = false;
        // 当焦点位于video组件内部重新赋予焦点，用来隐藏屏幕上的高亮框
        if (isChildFocused()) brls::Application::giveFocus(this);
        osdTopBox->setVisibility(brls::Visibility::INVISIBLE);
        osdBottomBox->setVisibility(brls::Visibility::INVISIBLE);
        osdLockBox->setVisibility(brls::Visibility::INVISIBLE);
    }

    if (current > this->hintLastShowTime) {
        this->hintBox->setVisibility(brls::Visibility::GONE);
        this->hintLastShowTime = 0;
    }

    // hot key
    this->buttonProcessing();

    // draw speed hint
    if (speedHintBox->getVisibility() == brls::Visibility::VISIBLE) {
        speedHintBox->frame(ctx);
        brls::Rect frame = speedHintLabel->getFrame();

        // a1-3 周期 800，范围 800 * 0.3 / 2 = 120, 0 - 120 - 0
        int ta1 = ((current >> 10) % 800) * 0.3;
        float tx = frame.getMinX() - 50;
        float ty = frame.getMinY() + 4.5;

        for (int i = 0; i < 3; i++) {
            int offx = tx + i * 15;
            int ta2 = (ta1 + i * 40) % 240;
            if (ta2 > 120) ta2 = 240 - ta2;

            nvgBeginPath(vg);
            nvgMoveTo(vg, offx, ty);
            nvgLineTo(vg, offx, ty + 12);
            nvgLineTo(vg, offx + 12, ty + 6);
            nvgFillColor(vg, a(nvgRGBA(255, 255, 255, ta2 + 80)));
            nvgClosePath(vg);
            nvgFill(vg);
        }
    }

    // cache info
    osdCenterBox->frame(ctx);

    // center hint
    osdInfoBox->frame(ctx);

    // draw video profile
    if (profile->getVisibility() == brls::Visibility::VISIBLE) {
        if (current - this->profileLastShowTime > 2000000) {
            profile->update();
            this->profileLastShowTime = current;
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
        brls::Logger::debug(
            "VideoView: size: {} / {} scale: {}", rect.getWidth(), rect.getHeight(), brls::Application::windowScale);
        MPVCore::instance().setFrameSize(rect);
    }
    oldRect = rect;
}

void VideoView::onChildFocusGained(View* directChild, View* focusedView) {
    Box::onChildFocusGained(directChild, focusedView);
    if (isOsdLock) {
        brls::Application::giveFocus(this->osdLockBox);
        return;
    }
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
            this->btnEpisode->setVisibility(brls::Visibility::VISIBLE);
            this->showEpisodeLabel->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex);
        },
        jellyfin::apiShowEpisodes, seriesId, query);
}

bool VideoView::playIndex(int index) {
    if (index < 0 || index >= (int)this->showEpisodes.size()) {
        return dismiss();
    }
    this->itemIndex = index;
    MPVCore::instance().reset();

    auto item = this->showEpisodes.at(this->itemIndex);
    this->itemId = item.Id;
    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(0);
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
            {"MediaSourceId", this->itemId},
            {"AudioStreamIndex", PlayerSetting::selectedAudio},
            {"SubtitleStreamIndex", PlayerSetting::selectedSubtitle},
            {"AllowAudioStreamCopy", true},
            {
                "DeviceProfile",
                {
                    {"MaxStreamingBitrate", MPVCore::VIDEO_QUALITY ? MPVCore::VIDEO_QUALITY << 20 : 1 << 24},
                    {
                        "DirectPlayProfiles",
                        {{
                            {"Type", "Video"},
#ifdef __SWITCH__
                            {"VideoCodec", "h264,hevc,av1,vp9"},
#endif
                        }},
                    },
                    {
                        "TranscodingProfiles",
                        {{
                            {"Container", "ts"},
                            {"Type", "Video"},
                            {"VideoCodec", MPVCore::VIDEO_CODEC},
                            {"AudioCodec", "aac,mp3,ac3,opus"},
                            {"Protocol", "hls"},
                        }},
                    },
                    {
                        "SubtitleProfiles",
                        {
                            {{"Format", "ass"}, {"Method", "External"}},
                            {{"Format", "ssa"}, {"Method", "External"}},
                            {{"Format", "srt"}, {"Method", "External"}},
                            {{"Format", "smi"}, {"Method", "External"}},
                            {{"Format", "sub"}, {"Method", "External"}},
                            {{"Format", "dvdsub"}, {"Method", "Embed"}},
                            {{"Format", "pgs"}, {"Method", "Embed"}},
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
                std::stringstream ssextra;
#ifdef _DEBUG
                for (auto& s : item.MediaStreams) {
                    brls::Logger::info("Track {} type {} => {}", s.Index, s.Type, s.DisplayTitle);
                }
#endif
                ssextra << "network-timeout=60";
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
                    this->itemSource = std::move(item);
                    return;
                }

                if (item.SupportsTranscoding) {
                    this->playMethod = jellyfin::methodTranscode;
                    mpv.setUrl(svr + item.TranscodingUrl, ssextra.str());
                    this->itemSource = std::move(item);
                    return;
                }
            }

            Dialog::show("Unsupport video format", []() { dismiss(); });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { dismiss(); });
        },
        jellyfin::apiPlayback, this->itemId);

    if (DanmakuCore::PLUGIN_ACTIVE) {
        this->requestDanmaku();
    }
}

void VideoView::reportStart() {
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"MediaSourceId", this->itemSource.Id},
            {"MaxStreamingBitrate", MPVCore::VIDEO_QUALITY},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
}

void VideoView::reportStop() {
    time_t ticks = MPVCore::instance().playback_time * jellyfin::PLAYTICKS;
    jellyfin::postJSON(
        {
            {"ItemId", this->itemId},
            {"PlayMethod", this->playMethod},
            {"PlaySessionId", this->playSessionId},
            {"PositionTicks", ticks},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStop);

    brls::Logger::debug("VideoView reportStop {}", this->playSessionId);
    this->playSessionId.clear();
}

void VideoView::reportPlay(bool isPaused) {
    time_t ticks = MPVCore::instance().video_progress * jellyfin::PLAYTICKS;
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

void VideoView::buttonProcessing() {
    // 获取按键数据
    brls::ControllerState state;
    input->updateUnifiedControllerState(&state);
    // 当OSD显示时上下左右切换选择按钮，持续显示OSD
    if (this->isOsdShown) {
        if (state.buttons[brls::BUTTON_NAV_RIGHT] || state.buttons[brls::BUTTON_NAV_LEFT] ||
            state.buttons[brls::BUTTON_NAV_UP] || state.buttons[brls::BUTTON_NAV_DOWN]) {
            if (this->osdState == OSDState::SHOWN) this->showOSD(true);
        }
    }
}

void VideoView::registerMpvEvent() {
    auto& mpv = MPVCore::instance();
    this->eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            if (MPVCore::OSD_ON_TOGGLE) {
                this->showOSD(true);
            }
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-pause.svg");
            this->reportPlay();
            this->profile->init(itemSource.Name, this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            if (MPVCore::OSD_ON_TOGGLE) {
                this->showOSD(false);
            }
            hideLoading(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            this->reportPlay(true);
            break;
        case MpvEventEnum::START_FILE:
            if (MPVCore::OSD_ON_TOGGLE) {
                this->showOSD(false);
            }
            break;
        case MpvEventEnum::LOADING_START:
            this->showLoading();
            break;
        case MpvEventEnum::LOADING_END:
            this->hideLoading();
            this->reportStart();
            break;
        case MpvEventEnum::MPV_STOP:
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
            auto& svr = AppConfig::instance().getUrl();
            // 移除其他备用链接
            mpv.command("playlist-clear");
            if (this->seekingRange == 0) {
                this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
            }
            for (auto& s : this->itemSource.MediaStreams) {
                if (s.Type == jellyfin::streamTypeSubtitle) {
                    if (s.DeliveryUrl.size() > 0 && (s.IsExternal || this->playMethod == jellyfin::methodTranscode)) {
                        std::string url = svr + s.DeliveryUrl;
                        mpv.command("sub-add", url.c_str(), "auto", s.DisplayTitle.c_str());
                    }
                }
            }
            if (PlayerSetting::selectedSubtitle > 0) {
                mpv.setInt("sid", PlayerSetting::selectedSubtitle);
            }
            break;
        }
        case MpvEventEnum::UPDATE_DURATION:
            if (this->seekingRange == 0) {
                this->rightStatusLabel->setText(misc::sec2Time(mpv.duration));
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            break;
        case MpvEventEnum::UPDATE_PROGRESS:
            if (this->seekingRange == 0) {
                this->leftStatusLabel->setText(misc::sec2Time(mpv.video_progress));
                this->osdSlider->setProgress(mpv.playback_time / mpv.duration);
            }
            if (mpv.video_progress % 10 == 0) this->reportPlay();
            break;
        case MpvEventEnum::VIDEO_SPEED_CHANGE:
            if (std::fabs(mpv.video_speed - 1) < 1e-5) {
                this->videoSpeedLabel->setText("main/player/speed"_i18n);
            } else {
                this->videoSpeedLabel->setText(fmt::format("{}x", mpv.video_speed));
            }
            break;
        case MpvEventEnum::END_OF_FILE:
            // 播放结束
            disableDimming(false);
            this->btnToggleIcon->setImageFromSVGRes("icon/ico-play.svg");
            this->playIndex(this->itemIndex + 1);
            break;
        case MpvEventEnum::CACHE_SPEED_CHANGE:
            // 仅当加载圈已经开始转起的情况显示缓存
            if (this->osdCenterBox->getVisibility() != brls::Visibility::GONE) {
                if (this->centerLabel->getVisibility() != brls::Visibility::VISIBLE)
                    this->centerLabel->setVisibility(brls::Visibility::VISIBLE);
                this->centerLabel->setText(MPVCore::instance().getCacheSpeed());
            }
            break;
        case MpvEventEnum::VIDEO_MUTE:
            this->btnVolumeIcon->setImageFromSVGRes("icon/ico-volume-off.svg");
            break;
        case MpvEventEnum::VIDEO_UNMUTE:
            this->btnVolumeIcon->setImageFromSVGRes("icon/ico-volume.svg");
            break;
        case MpvEventEnum::MPV_FILE_ERROR: {
            Dialog::show("main/player/error"_i18n, []() { dismiss(); });
            break;
        }
        default:;
        }
    });
    // 自定义的mpv事件
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == QUALITY_CHANGE) {
            this->playMedia(MPVCore::instance().playback_time * jellyfin::PLAYTICKS);
        }
    });
}

void VideoView::unRegisterMpvEvent() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCustomEvent()->unsubscribe(customEventSubscribeID);
}

// Loading
void VideoView::showLoading() {
    this->centerLabel->setVisibility(brls::Visibility::INVISIBLE);
    this->osdCenterBox->setVisibility(brls::Visibility::VISIBLE);
    disableDimming(false);
}

void VideoView::hideLoading(bool dimming) {
    this->osdCenterBox->setVisibility(brls::Visibility::GONE);
    disableDimming(dimming);
}

bool VideoView::toggleProfile() {
    if (profile->getVisibility() == brls::Visibility::VISIBLE) {
        profile->setVisibility(brls::Visibility::INVISIBLE);
        return false;
    }
    profile->setVisibility(brls::Visibility::VISIBLE);
    profile->update();
    return true;
}

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
        this->osdLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
        this->osdState = OSDState::SHOWN;
    } else {
        this->osdLastShowTime = 0xffffffff;
        this->osdState = OSDState::ALWAYS_ON;
    }
}

void VideoView::hideOSD() {
    this->osdLastShowTime = 0;
    this->osdState = OSDState::HIDDEN;
}

void VideoView::showHint(const std::string& value) {
    brls::Logger::debug("Video hint: {}", value);
    this->hintLabel->setText(value);
    this->hintBox->setVisibility(brls::Visibility::VISIBLE);
    this->hintLastShowTime = brls::getCPUTimeUsec() + VideoView::OSD_SHOW_TIME;
    this->showOSD();
}

bool VideoView::toggleOSDLock() {
    this->isOsdLock = !this->isOsdLock;
    if (this->isOsdLock) {
        this->osdLockIcon->setImageFromSVGRes("icon/player-lock.svg");
        osdTopBox->setVisibility(brls::Visibility::GONE);
        osdBottomBox->setVisibility(brls::Visibility::GONE);
        // 锁定时上下按键不可用
        osdLockBox->setCustomNavigationRoute(brls::FocusDirection::UP, "video/osd/lock/box");
        osdLockBox->setCustomNavigationRoute(brls::FocusDirection::DOWN, "video/osd/lock/box");
    } else {
        this->osdLockIcon->setImageFromSVGRes("icon/player-unlock.svg");
        // 手动设置上下按键的导航路线
        osdLockBox->setCustomNavigationRoute(brls::FocusDirection::UP, "video/osd/setting");
        osdLockBox->setCustomNavigationRoute(brls::FocusDirection::DOWN, "video/osd/icon/box");
    }
    return true;
}

bool VideoView::toggleSpeed() {
    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/speed"_i18n, {"2.0x", "1.75x", "1.5x", "1.25x", "1.0x", "0.75x", "0.5x"},
        [](int selected) { MPVCore::instance().setSpeed((200 - selected * 25) / 100.0f); },
        int(200 - MPVCore::instance().video_speed * 100) / 25);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool VideoView::toggleQuality() {
    auto& qualityOption = AppConfig::instance().getOptions(AppConfig::VIDEO_QUALITY);
    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/quality"_i18n, qualityOption.options,
        [&qualityOption](int selected) {
            if (MPVCore::VIDEO_QUALITY == qualityOption.values[selected]) return false;
            MPVCore::VIDEO_QUALITY = qualityOption.values[selected];
            AppConfig::instance().setItem(AppConfig::VIDEO_QUALITY, MPVCore::VIDEO_QUALITY);
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            return true;
        },
        AppConfig::instance().getValueIndex(AppConfig::VIDEO_QUALITY));
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool VideoView::toggleVolume(brls::View* view) {
    // 一直显示 OSD
    this->showOSD(false);
    auto theme = brls::Application::getTheme();
    auto container = new brls::Box();
    container->setHideClickAnimation(true);
    container->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View* view) {
        // 几秒后自动关闭 OSD
        this->showOSD(true);
        view->dismiss();
        // 保存结果
        return true;
    });
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container, [this, container]() {
        // 几秒后自动关闭 OSD
        this->showOSD(true);
        container->dismiss();
        // 保存结果
        return true;
    }));
    // 滑动条背景
    auto sliderBox = new brls::Box();
    sliderBox->setAlignItems(brls::AlignItems::CENTER);
    sliderBox->setHeight(40);
    sliderBox->setCornerRadius(4);
    sliderBox->setBackgroundColor(theme.getColor("color/grey_1"));
    float sliderX = view->getX() - 120;
    if (sliderX < 0) sliderX = 20;
    if (sliderX > brls::Application::ORIGINAL_WINDOW_WIDTH - 332)
        sliderX = brls::Application::ORIGINAL_WINDOW_WIDTH - 332;
    sliderBox->setTranslationX(sliderX);
    sliderBox->setTranslationY(view->getY() - 70);
    // 滑动条
    auto slider = new brls::Slider();
    slider->setMargins(8, 16, 8, 16);
    slider->setWidth(300);
    slider->setHeight(20);
    slider->setProgress(MPVCore::instance().getInt("volume") / 200.0f);
    slider->getProgressEvent()->subscribe([](float progress) { MPVCore::instance().setInt("volume", progress * 200); });
    sliderBox->addView(slider);
    container->addView(sliderBox);
    auto frame = new brls::AppletFrame(container);
    frame->setInFadeAnimation(true);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->setFooterVisibility(brls::Visibility::GONE);
    frame->setBackgroundColor(theme.getColor("brls/backdrop"));
    brls::Application::pushActivity(new brls::Activity(frame));
    return true;
}

bool VideoView::dismiss() {
    return brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
        auto mpvce = MPVCore::instance().getCustomEvent();
        mpvce->fire(VIDEO_CLOSE, nullptr);
    });
}

void VideoView::disableDimming(bool disable) {
    brls::Application::getPlatform()->disableScreenDimming(disable, "Playing video", AppVersion::getPackageName());
    brls::Application::setAutomaticDeactivation(!disable);
}

void VideoView::setChapters(const std::vector<jellyfin::MediaChapter>& chaps, time_t duration) {
    if (chaps.empty()) {
        this->btnVideoChapter->setVisibility(brls::Visibility::GONE);
        this->osdSlider->clearClipPoint();
        return;
    }

    std::vector<float> clips;
    for (auto& c : chaps) {
        clips.push_back(float(c.StartPositionTicks) / float(duration));
    }
    this->videoChapters = chaps;
    if (MPVCore::CLIP_POINT) {
        this->osdSlider->setClipPoint(clips);
    }
    this->btnVideoChapter->setVisibility(brls::Visibility::VISIBLE);
}

/// 获取视频弹幕
void VideoView::requestDanmaku() {
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
                if (child->Name()[0] != 'd') continue;  // 简易判断是不是弹幕
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
                this->setDanmakuEnable(brls::Visibility::VISIBLE);
            });

            brls::Logger::debug("DANMAKU: decode done: {}", items.size());

        } catch (const std::exception& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("request danmu: {}", ex.what());

            brls::sync([this]() {
                DanmakuCore::instance().reset();
                this->setDanmakuEnable(brls::Visibility::GONE);
            });
        }
    });
}

void VideoView::setDanmakuEnable(brls::Visibility v) {
    this->enableDanmaku = (v == brls::Visibility::VISIBLE);
    btnDanmakuIcon->setVisibility(v);
    btnDanmakuIcon->getParent()->setVisibility(v);
    if (enableDanmaku) {
        this->refreshDanmakuIcon();
    } else {
        btnDanmakuSettingIcon->setVisibility(v);
        btnDanmakuSettingIcon->getParent()->setVisibility(v);
    }
}

bool VideoView::toggleDanmaku() {
    if (!enableDanmaku) return false;
    DanmakuCore::DANMAKU_ON = !DanmakuCore::DANMAKU_ON;
    this->refreshDanmakuIcon();
    AppConfig::instance().setItem(AppConfig::DANMAKU_ON, DanmakuCore::DANMAKU_ON);
    return true;
}

void VideoView::refreshDanmakuIcon() {
    if (DanmakuCore::DANMAKU_ON) {
        this->btnDanmakuIcon->setImageFromSVGRes("icon/ico-danmu-switch-on.svg");
        btnDanmakuSettingIcon->setVisibility(brls::Visibility::VISIBLE);
        btnDanmakuSettingIcon->getParent()->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->btnDanmakuIcon->setImageFromSVGRes("icon/ico-danmu-switch-off.svg");
        // 当焦点刚好位于弹幕设置按钮时，这时通过快捷键关闭弹幕设置会导致焦点不会自动切换
        if (brls::Application::getCurrentFocus() == btnDanmakuSettingIcon) {
            brls::Application::giveFocus(btnDanmakuIcon);
        }
        btnDanmakuSettingIcon->setVisibility(brls::Visibility::GONE);
        btnDanmakuSettingIcon->getParent()->setVisibility(brls::Visibility::GONE);
    }
}