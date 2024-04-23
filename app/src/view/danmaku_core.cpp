//
// Created by fang on 2023/1/11.
//

#include <borealis/core/logger.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>

#include <cstdlib>
#include <utility>
#include <lunasvg.h>

#include "view/danmaku_core.hpp"
#include "utils/misc.hpp"

#ifndef MAX_DANMAKU_LENGTH
#define MAX_DANMAKU_LENGTH 4096
#endif

DanmakuItem::DanmakuItem(std::string content, const char *attributes) : msg(std::move(content)) {
    std::vector<std::string> attrs = misc::split(attributes, ',');

    if (attrs.size() < 9) {
        brls::Logger::error("error decode danmaku: {} {}", msg, attributes);
        type = -1;
        return;
    }
    time = atof(attrs[0].c_str());
    type = atoi(attrs[1].c_str());
    fontSize = atoi(attrs[2].c_str()) / 25.0f;
    fontColor = atoi(attrs[3].c_str());
    level = atoi(attrs[8].c_str());

    int r = (fontColor >> 16) & 0xff;
    int g = (fontColor >> 8) & 0xff;
    int b = fontColor & 0xff;
    isDefaultColor = (r & g & b) == 0xff;
    color = nvgRGB(r, g, b);
    color.a = DanmakuCore::DANMAKU_STYLE_ALPHA * 0.01;
    borderColor.a = DanmakuCore::DANMAKU_STYLE_ALPHA * 0.005;

    // 判断是否添加浅色边框
    if ((r * 299 + g * 587 + b * 114) < 60000) {
        borderColor = nvgRGB(255, 255, 255);
        borderColor.a = DanmakuCore::DANMAKU_STYLE_ALPHA * 0.005;
    }
}

void DanmakuItem::draw(NVGcontext *vg, float x, float y, float alpha, bool multiLine) const {
    float blur = DanmakuCore::DANMAKU_STYLE_FONT == DanmakuFontStyle::DANMAKU_FONT_SHADOW;
    float dilate = DanmakuCore::DANMAKU_STYLE_FONT == DanmakuFontStyle::DANMAKU_FONT_STROKE;
    float dx, dy;
    dx = dy = DanmakuCore::DANMAKU_STYLE_FONT == DanmakuFontStyle::DANMAKU_FONT_INCLINE;

    // background
    if (DanmakuCore::DANMAKU_STYLE_FONT != DanmakuFontStyle::DANMAKU_FONT_PURE) {
        nvgFontDilate(vg, dilate);
        nvgFontBlur(vg, blur);
        nvgFillColor(vg, a(borderColor, alpha));
        if (multiLine)
            nvgTextBox(vg, x + dx, y + dy, MAX_DANMAKU_LENGTH, msg.c_str(), nullptr);
        else
            nvgText(vg, x + dx, y + dy, msg.c_str(), nullptr);
    }

    // content
    nvgFontDilate(vg, 0.0f);
    nvgFontBlur(vg, 0.0f);
    nvgFillColor(vg, a(color, alpha));
    if (multiLine)
        nvgTextBox(vg, x + dx, y + dy, MAX_DANMAKU_LENGTH, msg.c_str(), nullptr);
    else
        nvgText(vg, x, y, msg.c_str(), nullptr);
}

NVGcolor DanmakuItem::a(NVGcolor color, float alpha) {
    color.a *= alpha;
    return color;
}

DanmakuCore::DanmakuCore() {
    event_id = MPVCore::instance().getEvent()->subscribe([this](MpvEventEnum e) {
        if (e == MpvEventEnum::LOADING_END) {
            this->refresh();
        } else if (e == MpvEventEnum::RESET) {
            this->reset();
        } else if (e == MpvEventEnum::VIDEO_SPEED_CHANGE) {
            this->setSpeed(MPVCore::instance().getSpeed());
        }
    });
}

DanmakuCore::~DanmakuCore() { MPVCore::instance().getEvent()->unsubscribe(event_id); }

void DanmakuCore::reset() {
    danmakuMutex.lock();
    lineNum = 20;
    scrollLines = std::vector<std::pair<float, float>>(20, {0, 0});
    centerLines = std::vector<float>(20, {0});
    this->danmakuData.clear();
    this->danmakuLoaded = false;
    danmakuIndex = 0;
    videoSpeed = MPVCore::instance().getSpeed();
    lineHeight = DANMAKU_STYLE_FONTSIZE * DANMAKU_STYLE_LINE_HEIGHT * 0.01f;
    danmakuMutex.unlock();
}

void DanmakuCore::loadDanmakuData(const std::vector<DanmakuItem> &data) {
    danmakuMutex.lock();
    this->danmakuData = data;
    if (!data.empty()) danmakuLoaded = true;
    std::sort(danmakuData.begin(), danmakuData.end());
    danmakuMutex.unlock();

    // 更新显示总行数等信息
    this->refresh();

    // 通过mpv来通知弹幕加载完成
    MPVCore::instance().getCustomEvent()->fire("DANMAKU_LOADED", nullptr);
}

void DanmakuCore::refresh() {
    danmakuMutex.lock();

    // 获取视频播放速度
    videoSpeed = MPVCore::instance().getSpeed();

    // 将当前屏幕第一条弹幕序号设为0
    danmakuIndex = 0;

    // 重置弹幕控制显示的信息
    for (auto &i : danmakuData) {
        i.showing = false;
        i.canShow = true;
    }

    // 重新设置最大显示的行数
    lineNum = brls::Application::windowHeight / DANMAKU_STYLE_FONTSIZE;
    while (scrollLines.size() < lineNum) {
        scrollLines.emplace_back(0, 0);
        centerLines.emplace_back(0);
    }

    // 重新设置行高
    lineHeight = DANMAKU_STYLE_FONTSIZE * DANMAKU_STYLE_LINE_HEIGHT * 0.01f;

    // 更新弹幕透明度
    for (auto &d : danmakuData) {
        d.color.a = DanmakuCore::DANMAKU_STYLE_ALPHA * 0.01;
        d.borderColor.a = DanmakuCore::DANMAKU_STYLE_ALPHA * 0.005;
    }

    // 重置弹幕每行的时间信息
    for (size_t k = 0; k < lineNum; k++) {
        scrollLines[k].first = 0;
        scrollLines[k].second = 0;
        centerLines[k] = 0;
    }
    danmakuMutex.unlock();
}

void DanmakuCore::setSpeed(double speed) {
    double oldSpeed = videoSpeed;
    videoSpeed = speed;
    int64_t currentTime = brls::getCPUTimeUsec();
    double factor = oldSpeed / speed;
    // 修改滚动弹幕的起始播放时间，满足修改后的时间在新速度下生成的位置不变。
    for (size_t j = this->danmakuIndex; j < this->danmakuData.size(); j++) {
        auto &i = this->danmakuData[j];
        if (i.type == 4 || i.type == 5) continue;
        if (!i.canShow) continue;
        if (i.time > MPVCore::instance().playback_time) return;
        i.startTime = currentTime - (currentTime - i.startTime) * factor;
    }
}

std::vector<DanmakuItem> DanmakuCore::getDanmakuData() {
    danmakuMutex.lock();
    std::vector<DanmakuItem> data = danmakuData;
    danmakuMutex.unlock();
    return data;
}

void DanmakuCore::draw(NVGcontext *vg, float x, float y, float width, float height, float alpha) {
    if (!DanmakuCore::DANMAKU_ON) return;
    if (!this->danmakuLoaded) return;
    if (danmakuData.empty()) return;

    int64_t currentTime = brls::getCPUTimeUsec();
    double playbackTime = MPVCore::instance().playback_time;
    float SECOND = 0.12f * DANMAKU_STYLE_SPEED;
    float CENTER_SECOND = 0.04f * DANMAKU_STYLE_SPEED;

    // Enable scissoring
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, width, height);

    // 设置基础字体
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontFaceId(vg, DanmakuCore::DANMAKU_FONT);
    nvgTextLineHeight(vg, 1);

    // 弹幕渲染质量
    if (DANMAKU_RENDER_QUALITY < 100) {
        nvgFontQuality(vg, 0.01f * DANMAKU_RENDER_QUALITY);
    }

    // 实际弹幕显示行数 （小于等于总行数且受弹幕显示区域限制）
    size_t LINES = height / lineHeight * DANMAKU_STYLE_AREA * 0.01f;
    if (LINES > lineNum) LINES = lineNum;

    //取出需要的弹幕
    float bounds[4];
    for (size_t j = this->danmakuIndex; j < this->danmakuData.size(); j++) {
        auto &i = this->danmakuData[j];
        // 溢出屏幕外或被过滤调不展示的弹幕
        if (!i.canShow) continue;

        // 正在展示中的弹幕
        if (i.showing) {
            if (i.type == 4 || i.type == 5) {
                //居中弹幕
                // 根据时间判断是否显示弹幕
                if (i.time > playbackTime || i.time + CENTER_SECOND < playbackTime) {
                    i.canShow = false;
                    continue;
                }

                // 画弹幕
                nvgFontSize(vg, DANMAKU_STYLE_FONTSIZE * i.fontSize);
                i.draw(vg, x + width / 2 - i.length / 2, y + i.line * lineHeight + 5, alpha);

                continue;
            } else if (i.type == 7) {
                if (!i.advancedAnimation.has_value() || !i.advancedAnimation->alpha.isRunning()) {
                    i.canShow = false;
                    continue;
                }
                nvgFontSize(vg, DANMAKU_STYLE_FONTSIZE * i.fontSize);
                nvgSave(vg);
                nvgTranslate(vg, x + i.advancedAnimation->transX, y + i.advancedAnimation->transY);
                nvgRotate(vg, i.advancedAnimation->rotateZ);
                if (i.advancedAnimation->rotateY > 0) {
                    // 近似模拟出 y 轴翻转的效果, 其实不太近似 :(
                    float ratio = fabs(1 - i.advancedAnimation->transX / width);
                    if (ratio > 1) ratio = 1;
                    float rotateY = i.advancedAnimation->rotateY * ratio / 2;
                    nvgScale(vg, 1 - rotateY / NVG_PI, 1.0f);
                    nvgSkewY(vg, rotateY);
                }
                i.draw(vg, 0, 0, i.advancedAnimation->alpha * alpha, true);
                nvgRestore(vg);
                continue;
            }
            //滑动弹幕
            float position = 0;
            if (MPVCore::instance().isPaused()) {
                // 暂停状态弹幕也要暂停
                position = i.speed * (playbackTime - i.time);
                i.startTime = currentTime - (playbackTime - i.time) / videoSpeed * 1e6;
            } else {
                // position = i.speed * (playbackTime - i.time) 是最精确的弹幕位置
                // 但是因为 playbackTime 是根据视频帧率设定的，直接使用此值会导致弹幕看起来卡顿
                // 这里以弹幕绘制的起始时间点为准，通过与当前时间值的差值计算来得到更精确的弹幕位置
                // 但是当 AV 不同步时，mpv会自动修正播放的进度，导致 playbackTime 和现实的时间脱离，不同弹幕间因此可能产生重叠
                position = i.speed * (currentTime - i.startTime) * videoSpeed / 1e6;
            }

            // 根据位置判断是否显示弹幕
            if (position > width + i.length) {
                i.showing = false;
                danmakuIndex = j + 1;
                continue;
            }

            // 画弹幕
            nvgFontSize(vg, DANMAKU_STYLE_FONTSIZE * i.fontSize);
            i.draw(vg, x + width - position, y + i.line * lineHeight + 5, alpha);
            continue;
        }

        // 添加即将出现的弹幕
        if (i.time < playbackTime) {
            // 排除已经应该暂停显示的弹幕
            if (i.type == 4 || i.type == 5) {
                // 底部或顶部弹幕
                if (i.time + CENTER_SECOND < playbackTime) {
                    continue;
                }
            } else if (i.time + SECOND < playbackTime) {
                // 滚动弹幕
                danmakuIndex = j + 1;
                continue;
            }

            /// 过滤弹幕
            i.canShow = false;
            // 1. 过滤显示的弹幕级别
            if (i.level < DANMAKU_FILTER_LEVEL) continue;

            if (i.type == 4) {
                // 2. 过滤底部弹幕
                if (!DANMAKU_FILTER_SHOW_BOTTOM) continue;
            } else if (i.type == 5) {
                // 3. 过滤顶部弹幕
                if (!DANMAKU_FILTER_SHOW_TOP) continue;
            } else if (i.type == 7) {
                // 4. 过滤高级弹幕
                if (!DANMAKU_FILTER_SHOW_ADVANCED) continue;
            } else {
                // 5. 过滤滚动弹幕
                if (!DANMAKU_FILTER_SHOW_SCROLL) continue;
            }

            // 6. 过滤彩色弹幕
            if (!i.isDefaultColor && !DANMAKU_FILTER_SHOW_COLOR) continue;

            // 7. 过滤失效弹幕
            if (i.type < 0) continue;

            // 处理高级弹幕动画
            if (i.type == 7) {
                if (!i.advancedAnimation.has_value()) continue;
                if (fabs(playbackTime - i.time) > 0.1) continue;
                i.showing = true;
                i.canShow = true;
                auto &ani = i.advancedAnimation;
                ani->alpha.stop();
                ani->transX.stop();
                ani->transY.stop();
                if (ani->path.size() < 2) continue;

                // 是否使用线形动画
                brls::EasingFunction easing =
                    ani->linear ? brls::EasingFunction::linear : brls::EasingFunction::cubicIn;

                // 是否使用相对坐标
                float relativeSizeX = 1.0f, relativeSizeY = 1.0f;
                if (ani->relativeLayout) {
                    relativeSizeX = width;
                    relativeSizeY = height;
                }

                ani->transX.reset(ani->path[0].x * relativeSizeX);
                ani->transY.reset(ani->path[0].y * relativeSizeY);
                ani->alpha.reset(ani->alpha1);

                // 起点停留
                if (ani->time1 > 0) {
                    ani->transX.addStep(ani->path[0].x * relativeSizeX, ani->time1);
                    ani->transY.addStep(ani->path[0].y * relativeSizeY, ani->time1);
                }

                // 路径动画
                if (ani->time2 > 0) {
                    float timeD = ani->time2 / (ani->path.size() - 1);
                    for (size_t p = 1; p < ani->path.size(); p++) {
                        ani->transX.addStep(ani->path[p].x * relativeSizeX, timeD, easing);
                        ani->transY.addStep(ani->path[p].y * relativeSizeY, timeD, easing);
                    }
                }

                // 结束点停留
                if (ani->time3 > 0) {
                    ani->transX.addStep(ani->path[ani->path.size() - 1].x * relativeSizeX, ani->time3);
                    ani->transY.addStep(ani->path[ani->path.size() - 1].y * relativeSizeY, ani->time3);
                }

                // 半透明
                ani->alpha.addStep(ani->alpha2, ani->wholeTime);
                ani->alpha.start();
                ani->transX.start();
                ani->transY.start();
                continue;
            }

            /// 处理即将要显示的弹幕
            nvgFontSize(vg, DANMAKU_STYLE_FONTSIZE * i.fontSize);
            nvgTextBounds(vg, 0, 0, i.msg.c_str(), nullptr, bounds);
            i.length = bounds[2] - bounds[0];
            i.speed = (width + i.length) / SECOND;
            i.showing = true;
            for (size_t k = 0; k < LINES; k++) {
                if (i.type == 4) {
                    //底部
                    if (i.time < centerLines[LINES - k - 1]) continue;

                    i.line = LINES - k - 1;
                    centerLines[LINES - k - 1] = i.time + CENTER_SECOND;
                    i.canShow = true;
                    break;
                } else if (i.type == 5) {
                    //顶部
                    if (i.time < centerLines[k]) continue;

                    i.line = k;
                    centerLines[k] = i.time + CENTER_SECOND;
                    i.canShow = true;
                    break;
                } else {
                    //滚动
                    if (i.time < scrollLines[k].first || i.time + width / i.speed < scrollLines[k].second) continue;
                    i.line = k;
                    // 一条弹幕完全展示的时间点，同一行的其他弹幕需要在这之后出现
                    scrollLines[k].first = i.time + i.length / i.speed;
                    // 一条弹幕展示结束的时间点，同一行的其他弹幕到达屏幕左侧的时间应该在这之后。
                    scrollLines[k].second = i.time + SECOND;
                    i.canShow = true;
                    i.startTime = currentTime;
                    // 如果当前时间点弹幕已经出现在屏幕上了，那么反向推算出弹幕开始的现实时间
                    if (playbackTime - i.time > 0.2) i.startTime -= (playbackTime - i.time) / videoSpeed * 1e6;
                    break;
                }
            }
            // 循环之外的弹幕因为 canShow 为 false 所以不会显示
        } else {
            // 当前没有需要显示或等待显示的弹幕，结束循环
            break;
        }
    }

    nvgRestore(vg);
}
