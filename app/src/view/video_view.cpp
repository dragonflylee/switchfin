#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "view/video_progress_slider.hpp"
#include "view/player_setting.hpp"
#include "view/video_profile.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include <sstream>

using namespace brls::literals;

VideoView::VideoView(const jellyfin::MediaItem& item) : itemId(item.Id) {
    this->inflateFromXMLRes("xml/view/video_view.xml");
    brls::Logger::debug("VideoView: create with item {}", itemId);
    this->setHideHighlightBorder(true);
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    // 停止正在播放的音乐
    MPVCore::instance().stop();

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
        "hints/back"_i18n, brls::BUTTON_B, [](brls::View* view) { return popActivity(); }, true);

    this->registerAction(
        "\uE08F", brls::BUTTON_LB,
        [this](brls::View* view) -> bool {
            this->seekingRange -= MPVCore::SEEKING_STEP;
            this->requestSeeking();
            return true;
        },
        false, true);

    this->registerAction(
        "\uE08E", brls::BUTTON_RB,
        [this](brls::View* view) -> bool {
            this->seekingRange += MPVCore::SEEKING_STEP;
            this->requestSeeking();
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
    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            brls::Application::giveFocus(this);
            auto& mpv = MPVCore::instance();
            // 当长按时已经加速，则忽视此次加速
            switch (status.state) {
            case brls::GestureState::UNSURE: {
                // 长按加速
                if (std::fabs(mpv.getSpeed() - 1) > 10e-2) {
                    this->ignoreSpeed = true;
                    break;
                }
                this->ignoreSpeed = false;
                brls::cancelDelay(this->speedIter);
                ASYNC_RETAIN
                this->speedIter = brls::delay(500, [ASYNC_TOKEN]() {
                    ASYNC_RELEASE
                    float cur = MPVCore::VIDEO_SPEED == 100 ? 2.0 : MPVCore::VIDEO_SPEED * 0.01f;
                    MPVCore::instance().setSpeed(cur);
                    // 绘制临时加速标识
                    this->speedHintLabel->setText(fmt::format(fmt::runtime("main/player/speed_up"_i18n), cur));
                    this->speedHintBox->setVisibility(brls::Visibility::VISIBLE);
                });
                break;
            }
            case brls::GestureState::FAILED:
            case brls::GestureState::INTERRUPTED: {
                // 打断加速
                if (!this->ignoreSpeed) {
                    brls::cancelDelay(this->speedIter);
                    mpv.setSpeed(1.0f);
                    this->speedHintBox->setVisibility(brls::Visibility::GONE);
                }
                break;
            }
            case brls::GestureState::END: {
                // 打断加速
                if (!ignoreSpeed) {
                    brls::cancelDelay(this->speedIter);
                    mpv.setSpeed(1.0f);
                    if (this->speedHintBox->getVisibility() == brls::Visibility::VISIBLE) {
                        this->speedHintBox->setVisibility(brls::Visibility::GONE);
                        // 正在加速时抬起手指，不触发后面 OSD 相关内容，直接结束此次事件
                        break;
                    }
                }
                // 处理点击事件
                const int CHECK_TIME = 200000;
                switch (this->clickState) {
                case ClickState::IDLE: {
                    this->pressTime = brls::getCPUTimeUsec();
                    this->clickState = ClickState::CLICK_DOUBLE;
                    // 单击切换 OSD，设置一个延迟用来等待双击结果
                    ASYNC_RETAIN
                    this->tapIter = brls::delay(200, [ASYNC_TOKEN]() {
                        ASYNC_RELEASE
                        this->toggleOSD();
                    });
                    break;
                }
                case ClickState::CLICK_DOUBLE: {
                    brls::cancelDelay(this->tapIter);
                    if (brls::getCPUTimeUsec() - this->pressTime < CHECK_TIME) {
                        // 双击切换播放状态
                        mpv.togglePlay();
                        this->clickState = ClickState::IDLE;
                    } else {
                        // 单击切换 OSD，设置一个延迟用来等待双击结果
                        this->pressTime = brls::getCPUTimeUsec();
                        this->clickState = ClickState::CLICK_DOUBLE;
                        ASYNC_RETAIN
                        this->tapIter = brls::delay(200, [ASYNC_TOKEN]() {
                            ASYNC_RELEASE
                            this->toggleOSD();
                        });
                    }
                    break;
                }
                default:;
                }
            }
            default:;
            }
        }));

    /// 播放/暂停 按钮
    this->btnToggle->registerClickAction([](...) {
        MPVCore::instance().togglePlay();
        return true;
    });
    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnToggle));

    /// 播放控制
    this->btnBackward->registerClickAction([this](...) { return this->playNext(-1); });
    this->btnBackward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnBackward));

    this->btnForward->registerClickAction([this](...) { return this->playNext(); });
    this->btnForward->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnForward));

    this->registerAction("main/player/toggle"_i18n, brls::BUTTON_A, [this](brls::View* view) {
        MPVCore::instance().togglePlay();
        if (MPVCore::OSD_ON_TOGGLE) {
            this->showOSD(true);
        }
        return true;
    });

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
        if (this->chapters.empty()) return false;

        int selectedChapter = -1;
        time_t ticks = MPVCore::instance().video_progress * jellyfin::PLAYTICKS;
        std::vector<std::string> values;
        for (auto& item : this->chapters) {
            values.push_back(item.Name);
            if (item.StartPositionTicks <= ticks) selectedChapter++;
        }

        brls::Dropdown* dropdown = new brls::Dropdown(
            "main/player/chapter"_i18n, values,
            [this](int selected) {
                int64_t offset = this->chapters[selected].StartPositionTicks;
                MPVCore::instance().seek(offset / jellyfin::PLAYTICKS);
            },
            selectedChapter);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    this->btnVideoChapter->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnVideoChapter));

    this->chapters = item.Chapters;
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

void VideoView::requestSeeking() {
    auto& mpv = MPVCore::instance();
    if (mpv.duration <= 0) {
        this->seekingRange = 0;
        return;
    }
    double progress = (mpv.playback_time + this->seekingRange) / mpv.duration;

    if (progress < 0) {
        progress = 0;
        this->seekingRange = (int64_t)mpv.playback_time * -1;
    } else if (progress > 1) {
        progress = 1;
        this->seekingRange = mpv.duration;
    }

    showOSD(false);
    osdSlider->setProgress(progress);
    leftStatusLabel->setText(misc::sec2Time(mpv.duration * progress));

    // 延迟触发跳转进度
    brls::cancelDelay(this->seekingIter);
    ASYNC_RETAIN
    this->seekingIter = brls::delay(400, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        MPVCore::instance().seek(this->seekingRange, "relative");
        this->seekingRange = 0;
    });
}

void VideoView::showSetting() {
    brls::View* setting = new PlayerSetting(this->itemSource);
    brls::Application::pushActivity(new brls::Activity(setting));
    // 手动将焦点赋给设置页面
    brls::sync([setting]() { brls::Application::giveFocus(setting); });
}

void VideoView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    auto& mpv = MPVCore::instance();
    if (!mpv.isValid()) return;

    // draw video
    mpv.draw(this->getFrame(), this->getAlpha());

    // draw osd
    brls::Time current = brls::getCPUTimeUsec();
    if (this->osdState == OSDState::ALWAYS_ON || current < this->osdLastShowTime) {
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
        return popActivity();
    }
    MPVCore::instance().reset();

    auto item = this->showEpisodes.at(this->itemIndex);
    this->itemId = item.Id;
    this->chapters = item.Chapters;
    this->playMedia(0);
    this->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
    this->btnBackward->setVisibility(this->itemIndex > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->btnForward->setVisibility(
        this->itemIndex + 1 < this->showEpisodes.size() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    return true;
}

void VideoView::playMedia(const time_t seekTicks) {
    this->btnVideoChapter->setVisibility(this->chapters.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

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

            Dialog::show("Unsupport video format", []() { popActivity(); });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { popActivity(); });
        },
        jellyfin::apiPlayback, this->itemId);
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
            disableDimming(false);
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
            this->playNext();
            break;
        case MpvEventEnum::CACHE_SPEED_CHANGE:
            // 仅当加载圈已经开始转起的情况显示缓存
            if (this->osdCenterBox->getVisibility() != brls::Visibility::GONE) {
                this->centerLabel->setText(MPVCore::instance().getCacheSpeed());
            }
            break;
        case MpvEventEnum::MPV_FILE_ERROR: {
            Dialog::show("main/player/error"_i18n, []() { popActivity(); });
            break;
        }
        default:;
        }
    });
    // 自定义的mpv事件
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == VideoView::QUALITY_CHANGE) {
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

void VideoView::hideLoading() {
    this->osdCenterBox->setVisibility(brls::Visibility::GONE);
    disableDimming(true);
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

bool VideoView::toggleSpeed() {
    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/speed"_i18n, {"2.0x", "1.75x", "1.5x", "1.25x", "1.0x", "0.75x", "0.5x"},
        [](int selected) { MPVCore::instance().setSpeed((200 - selected * 25) / 100.0f); },
        int(200 - MPVCore::instance().video_speed * 100) / 25);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    brls::sync([dropdown]() { brls::Application::giveFocus(dropdown); });
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
            MPVCore::instance().getCustomEvent()->fire(VideoView::QUALITY_CHANGE, nullptr);
            return true;
        },
        AppConfig::instance().getValueIndex(AppConfig::VIDEO_QUALITY));
    brls::Application::pushActivity(new brls::Activity(dropdown));
    brls::sync([dropdown]() { brls::Application::giveFocus(dropdown); });
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

    // 手动将焦点赋给音量组件
    brls::sync([container]() { brls::Application::giveFocus(container); });
    return true;
}

bool VideoView::popActivity() {
    return brls::Application::popActivity(brls::TransitionAnimation::NONE, []() {
        auto mpvce = MPVCore::instance().getCustomEvent();
        mpvce->fire(VIDEO_CLOSE, nullptr);
    });
}

void VideoView::disableDimming(bool disable) {
    brls::Application::getPlatform()->disableScreenDimming(disable, "Playing video", AppVersion::getPackageName());
    brls::Application::setAutomaticDeactivation(!disable);
}