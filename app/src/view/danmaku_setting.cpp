//
// Created by fang on 2023/1/10.
//

#include "view/danmaku_setting.hpp"
#include "view/danmaku_core.hpp"
#include "view/button_close.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

DanmakuSetting::DanmakuSetting() {
    this->inflateFromXMLRes("xml/view/danmaku_setting.xml");
    brls::Logger::debug("Fragment DanmakuSetting: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

    closebtn->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    auto& conf = AppConfig::instance();

    this->cellArea->init("main/danmaku/style/area"_i18n,
        {
            "main/danmaku/style/area_1_4"_i18n,
            "main/danmaku/style/area_2_4"_i18n,
            "main/danmaku/style/area_3_4"_i18n,
            "main/danmaku/style/area_4_4"_i18n,
        },
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_AREA, 3), [](int data) {
            DanmakuCore::DANMAKU_STYLE_AREA = 25 + data * 25;
            AppConfig::instance().setItem(AppConfig::DANMAKU_STYLE_AREA, DanmakuCore::DANMAKU_STYLE_AREA);
            DanmakuCore::instance().refresh();
            return true;
        });

    auto alpha = conf.getOptions(AppConfig::DANMAKU_STYLE_ALPHA);
    this->cellAlpha->init("main/danmaku/style/alpha"_i18n, alpha.options,
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_ALPHA, 7), [alpha](int data) {
            DanmakuCore::DANMAKU_STYLE_ALPHA = alpha.values[data];
            AppConfig::instance().setItem(AppConfig::DANMAKU_STYLE_ALPHA, DanmakuCore::DANMAKU_STYLE_ALPHA);
            DanmakuCore::instance().refresh();
            return true;
        });

    auto font = conf.getOptions(AppConfig::DANMAKU_STYLE_FONTSIZE);
    this->cellFontsize->init("main/danmaku/style/fontsize"_i18n, font.options,
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_FONTSIZE, 2), [font](int data) {
            DanmakuCore::DANMAKU_STYLE_FONTSIZE = font.values[data];
            AppConfig::instance().setItem(AppConfig::DANMAKU_STYLE_FONTSIZE, DanmakuCore::DANMAKU_STYLE_FONTSIZE);
            DanmakuCore::instance().refresh();
            return true;
        });

    auto height = conf.getOptions(AppConfig::DANMAKU_STYLE_LINE_HEIGHT);
    this->cellLineHeight->init("main/danmaku/style/line_height"_i18n, height.options,
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_LINE_HEIGHT, 1), [height](int data) {
            DanmakuCore::DANMAKU_STYLE_LINE_HEIGHT = height.values[data];
            AppConfig::instance().setItem(AppConfig::DANMAKU_STYLE_LINE_HEIGHT, DanmakuCore::DANMAKU_STYLE_LINE_HEIGHT);
            DanmakuCore::instance().refresh();
            return true;
        });

    auto speed = conf.getOptions(AppConfig::DANMAKU_STYLE_SPEED);
    this->cellSpeed->init("main/danmaku/style/speed"_i18n,
        {
            "main/danmaku/style/speed_slow_plus"_i18n,
            "main/danmaku/style/speed_slow"_i18n,
            "main/danmaku/style/speed_moderate"_i18n,
            "main/danmaku/style/speed_fast"_i18n,
            "main/danmaku/style/speed_fast_plus"_i18n,
        },
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_SPEED, 2), [speed](int data) {
            DanmakuCore::DANMAKU_STYLE_SPEED = speed.values[data];
            AppConfig::instance().setItem(AppConfig::DANMAKU_STYLE_SPEED, DanmakuCore::DANMAKU_STYLE_SPEED);
            DanmakuCore::instance().refresh();
            return true;
        });

    this->cellBackground->init("main/danmaku/style/font"_i18n,
        {
            "main/danmaku/style/font_stroke"_i18n,
            "main/danmaku/style/font_incline"_i18n,
            "main/danmaku/style/font_shadow"_i18n,
            "main/danmaku/style/font_pure"_i18n,
        },
        conf.getValueIndex(AppConfig::DANMAKU_STYLE_FONT, 2), [&conf](int data) {
            auto& fonts = conf.getOptions(AppConfig::DANMAKU_STYLE_FONT);
            DanmakuCore::DANMAKU_STYLE_FONT = DanmakuFontStyle{data};
            conf.setItem(AppConfig::DANMAKU_STYLE_FONT, fonts.options[data]);
        });

    auto perf = conf.getOptions(AppConfig::DANMAKU_RENDER_QUALITY);
    this->cellRenderPerf->init("main/danmaku/performance/render"_i18n, perf.options,
        conf.getOptionIndex(AppConfig::DANMAKU_RENDER_QUALITY), [perf](int data) {
            DanmakuCore::DANMAKU_RENDER_QUALITY = perf.values[data];
            AppConfig::instance().setItem(AppConfig::DANMAKU_RENDER_QUALITY, DanmakuCore::DANMAKU_RENDER_QUALITY);
        });
}

DanmakuSetting::~DanmakuSetting() { brls::Logger::debug("Fragment DanmakuSetting: delete"); }

brls::View* DanmakuSetting::create() { return new DanmakuSetting(); }

bool DanmakuSetting::isTranslucent() { return true; }

brls::View* DanmakuSetting::getDefaultFocus() { return this->settings->getDefaultFocus(); }
