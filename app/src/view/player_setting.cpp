#include <borealis/views/hint.hpp>
#include "utils/config.hpp"
#include "utils/event.hpp"
#include "view/button_close.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"

using namespace brls::literals;

/// Live subtitle-sync overlay. Translucent so the video AND its subtitles
/// stay visible underneath: the user nudges the delay with LEFT/RIGHT and
/// immediately sees whether it lines up (no more adjusting blind). Centered
/// so it never sits on top of the subtitles it is meant to align.
class SubsyncOverlay : public brls::Box {
public:
    SubsyncOverlay() {
        auto theme = brls::Application::getTheme();
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setGrow(1.0f);
        this->setFocusable(true);
        this->setHideHighlightBackground(true);
        this->setHideHighlightBorder(true);

        auto* panel = new brls::Box(brls::Axis::COLUMN);
        panel->setAlignItems(brls::AlignItems::CENTER);
        panel->setCornerRadius(12);
        panel->setBackgroundColor(nvgRGBA(0x1C, 0x1C, 0x1C, 0xF0));
        panel->setPadding(26, 52, 26, 52);

        auto* heading = new brls::Label();
        heading->setText("main/setting/playback/subsync"_i18n);
        heading->setFontSize(18);
        heading->setTextColor(theme.getColor("font/grey"));
        heading->setMarginBottom(20);
        panel->addView(heading);

        // row: ◀  (value)  ▶  — chevrons cue LEFT/RIGHT, muted vs the value
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);

        auto* left = new brls::Label();
        left->setText("◀");
        left->setFontSize(22);
        left->setTextColor(theme.getColor("font/grey"));
        left->setMarginRight(34);
        row->addView(left);

        this->value = new brls::Label();
        this->value->setFontSize(26);
        this->value->setWidth(120);
        this->value->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        this->value->setTextColor(theme.getColor("color/white"));
        row->addView(this->value);

        auto* right = new brls::Label();
        right->setText("▶");
        right->setFontSize(22);
        right->setTextColor(theme.getColor("font/grey"));
        right->setMarginLeft(34);
        row->addView(right);

        panel->addView(row);

        // back hint inside the card, native key glyph like the rest of the app
        auto* hint = new brls::Label();
        hint->setText(brls::Hint::getKeyIcon(brls::BUTTON_B) + "  " + "hints/back"_i18n);
        hint->setFontSize(16);
        hint->setTextColor(theme.getColor("font/grey"));
        hint->setMarginTop(22);
        panel->addView(hint);

        this->addView(panel);

        // local source of truth for the display: mpv set/get is async, so
        // re-reading right after setDouble returns the OLD value (the first
        // nudge then never showed, and the closed value didn't match what
        // re-opened). Seed from mpv once, then track locally.
        this->delay = MPVCore::instance().getDouble("sub-delay");
        this->refresh();

        this->registerAction(
            "main/setting/playback/subsync"_i18n, brls::BUTTON_NAV_LEFT,
            [this](brls::View*) {
                this->nudge(-0.1);
                return true;
            },
            true, true);
        this->registerAction(
            "", brls::BUTTON_NAV_RIGHT,
            [this](brls::View*) {
                this->nudge(0.1);
                return true;
            },
            true, true);
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    bool isTranslucent() override { return true; }
    brls::View* getDefaultFocus() override { return this; }

private:
    brls::Label* value = nullptr;
    double delay = 0;

    void nudge(double d) {
        this->delay += d;
        MPVCore::instance().setDouble("sub-delay", this->delay);
        this->refresh();
    }
    void refresh() {
        this->value->setText(fmt::format("{:+.1f} s", this->delay));
    }
};

PlayerSetting::PlayerSetting() {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
    brls::Logger::debug("PlayerSetting: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    auto& mpv = MPVCore::instance();
    auto& conf = AppConfig::instance();

/// Fullscreen
#if (defined(__APPLE__) || defined(__linux__) || defined(_WIN32)) && !defined(ANDROID)
    btnFullscreen->init(
        "main/setting/others/fullscreen"_i18n, conf.getItem(AppConfig::FULLSCREEN, false), [](bool value) {
            VideoContext::FULLSCREEN = value;
            AppConfig::instance().setItem(AppConfig::FULLSCREEN, value);
            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
        });

    btnAlwaysOnTop->init(
        "main/setting/others/always_on_top"_i18n, conf.getItem(AppConfig::ALWAYS_ON_TOP, false), [](bool value) {
            AppConfig::instance().setItem(AppConfig::ALWAYS_ON_TOP, value);
            brls::Application::getPlatform()->setWindowAlwaysOnTop(value);
        });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnAlwaysOnTop->setVisibility(brls::Visibility::GONE);
#endif

    btnOSDOnToggle->init(
        "main/setting/playback/osd_on_toggle"_i18n, conf.getItem(AppConfig::OSD_ON_TOGGLE, true), [&conf](bool value) {
            MPVCore::OSD_ON_TOGGLE = value;
            conf.setItem(AppConfig::OSD_ON_TOGGLE, value);
        });

    /// Player mirror
    btnVideoMirror->init("main/setting/filter/mirror"_i18n,
        {
            "hints/off"_i18n,
            "main/setting/filter/hflip"_i18n,
            "main/setting/filter/vflip"_i18n,
        },
        MPVCore::VIDEO_FILTER, [&mpv](int value) {
            MPVCore::VIDEO_FILTER = value;
            switch (value) {
            case 1:
                mpv.command("set", "vf", "hflip");
                break;
            case 2:
                mpv.command("set", "vf", "vflip");
                break;
            default:
                mpv.command("set", "vf", "");
            }
            // 如果正在使用硬解，那么将硬解更新为 auto-copy，避免直接硬解因为不经过 cpu 处理导致镜像翻转无效
            if (MPVCore::HARDWARE_DEC) {
                const char* hwdec = value > 0 ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD.c_str();
                mpv.command("set", "hwdec", hwdec);
                brls::Logger::info("MPV hardware decode: {}", hwdec);
            }
        });

    btnVideoRotation->init("main/setting/filter/rotation"_i18n,
        {
            "hints/off"_i18n,
            "90",
            "180",
            "270",
        },
        MPVCore::VIDEO_ROTATION, [&mpv](int value) {
            MPVCore::VIDEO_ROTATION = value;
            switch (value) {
            case 1:
                mpv.command("set", "video-rotate", "90");
                return;
            case 2:
                mpv.command("set", "video-rotate", "180");
                return;
            case 3:
                mpv.command("set", "video-rotate", "270");
                return;
            default:
                mpv.command("set", "video-rotate", "0");
            }
        });

    /// Player aspect
    btnVideoAspect->init("main/setting/aspect/header"_i18n,
        {
            "main/setting/aspect/auto"_i18n,
            "main/setting/aspect/stretch"_i18n,
            "main/setting/aspect/crop"_i18n,
            "4:3",
            "16:9",
        },
        conf.getOptionIndex(AppConfig::PLAYER_ASPECT), [&mpv, &conf](int value) {
            auto& opt = conf.getOptions(AppConfig::PLAYER_ASPECT);
            MPVCore::VIDEO_ASPECT = opt.options.at(value);
            mpv.setAspect(MPVCore::VIDEO_ASPECT);
            conf.setItem(AppConfig::PLAYER_ASPECT, MPVCore::VIDEO_ASPECT);
        });

    btnEqualizerReset->registerClickAction([this](View* view) {
        btnEqualizerBrightness->slider->setProgress(0.5f);
        btnEqualizerContrast->slider->setProgress(0.5f);
        btnEqualizerSaturation->slider->setProgress(0.5f);
        btnEqualizerGamma->slider->setProgress(0.5f);
        btnEqualizerHue->slider->setProgress(0.5f);
        return true;
    });
    registerHideBackground(btnEqualizerReset);
    setupEqualizer(btnEqualizerBrightness, "main/setting/equalizer/brightness"_i18n, Equalizer::BRIGHTNESS,
        mpv.getDouble("brightness"));
    setupEqualizer(
        btnEqualizerContrast, "main/setting/equalizer/contrast"_i18n, Equalizer::CONTRAST, mpv.getDouble("contrast"));
    setupEqualizer(btnEqualizerSaturation, "main/setting/equalizer/saturation"_i18n, Equalizer::SATURATION,
        mpv.getDouble("saturation"));
    setupEqualizer(btnEqualizerGamma, "main/setting/equalizer/gamma"_i18n, Equalizer::GAMMA, mpv.getDouble("hue"));
    setupEqualizer(btnEqualizerHue, "main/setting/equalizer/hue"_i18n, Equalizer::HUE, mpv.getDouble("gamma"));
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("PlayerSetting: delete"); }

void PlayerSetting::showAudioMenu(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    // embedded tracks (direct play, or the single track of a transcode)
    std::vector<std::string> embedded;
    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "audio") continue;
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
        if (title.empty()) title = fmt::format("{} {}", "main/player/audio"_i18n, embedded.size() + 1);
        embedded.push_back(title);
    }
    if (embedded.size() > 1) {
        int current = (int)(mpv.getInt("aid", 1) - 1);
        auto* dropdown = new brls::Dropdown(
            "main/player/audio"_i18n, embedded,
            [](int selected) {
                selectedAudio = selected + 1;
                MPVCore::instance().setInt("aid", selectedAudio);
            },
            current < 0 ? 0 : current);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return;
    }

    // transcode: tracks come from the Plex Media (re-transcode on change)
    std::vector<std::string> names;
    std::vector<int64_t> ids;
    if (src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeAudio) continue;
            names.push_back(s.displayTitle);
            ids.push_back(s.id);
        }
    }
    if (names.size() > 1) {
        int current = 0;
        for (size_t i = 0; i < ids.size(); i++)
            if (ids[i] == selectedAudio) current = (int)i;
        auto* dropdown = new brls::Dropdown(
            "main/player/audio"_i18n, names,
            [ids](int selected) {
                selectedAudio = ids[selected];
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            },
            current);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return;
    }

    brls::Application::notify("main/player/audio"_i18n);
}

void PlayerSetting::showSubtitleMenu(const plex::Media* src) {
    auto& mpv = MPVCore::instance();

    std::vector<std::string> names = {"main/player/none"_i18n};
    // each entry's selection action; index aligned with `names`
    std::vector<std::function<void()>> actions = {[]() {
        selectedSubtitle = 0;
        MPVCore::instance().setInt("sid", 0);
    }};
    int current = 0;

    // embedded subtitle tracks (sid). Sidecar Plex subs are sub-add'ed into
    // mpv on direct play, so they show up here too.
    int64_t count = mpv.getInt("track-list/count");
    int64_t sidActive = mpv.getInt("sid");
    for (int64_t n = 0; n < count; n++) {
        if (mpv.getString(fmt::format("track-list/{}/type", n)) != "sub") continue;
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        if (title.empty()) title = mpv.getString(fmt::format("track-list/{}/lang", n));
        int64_t id = mpv.getInt(fmt::format("track-list/{}/id", n));
        if (title.empty()) title = fmt::format("{} {}", "main/player/subtitle"_i18n, id);
        if (id == sidActive) current = (int)names.size();
        names.push_back(title);
        actions.push_back([id]() {
            selectedSubtitle = id;
            MPVCore::instance().setInt("sid", id);
        });
    }

    // transcode: no embedded subs in the HLS stream -> Plex stream ids
    // (burned in, re-transcode on change)
    if (names.size() == 1 && src != nullptr && !src->parts.empty()) {
        for (auto& s : src->parts.front().streams) {
            if (s.streamType != plex::streamTypeSubtitle) continue;
            int64_t id = s.id;
            if (id == selectedSubtitle) current = (int)names.size();
            names.push_back(s.displayTitle);
            actions.push_back([id]() {
                selectedSubtitle = id;
                MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            });
        }
    }

    // trailing entry: subtitle sync (sub-delay) — opens a presets picker
    size_t syncIndex = names.size();
    double subDelay = mpv.getDouble("sub-delay");
    names.push_back(fmt::format("{} ({:+.1f} s)", "main/setting/playback/subsync"_i18n, subDelay));

    auto* dropdown = new brls::Dropdown(
        "main/player/subtitle"_i18n, names,
        [actions, syncIndex](int selected) {
            if ((size_t)selected != syncIndex) {
                if (selected >= 0 && (size_t)selected < actions.size()) actions[selected]();
                return;
            }
            // open the live sync overlay, deferred so this dropdown finishes
            // closing first (otherwise its pop would immediately eat the
            // overlay we just pushed — "nothing happens")
            brls::sync([]() { brls::Application::pushActivity(new brls::Activity(new SubsyncOverlay())); });
        },
        current);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void PlayerSetting::setupEqualizer(brls::SliderCell* cell, const std::string& title, Equalizer item, double initValue) {
    if (initValue < -100)
        initValue = -100;
    else if (initValue > 100)
        initValue = 100;

    cell->detail->setWidth(50);
    cell->title->setWidth(116);
    cell->title->setMarginRight(0);
    cell->slider->setStep(0.05f);
    cell->slider->setMarginRight(0);
    cell->slider->setPointerSize(20);
    cell->setDetailText(fmt::format("{:.0f}", initValue));
    cell->init(title, (initValue + 100) * 0.005f, [cell, item](float value) {
        auto& mpv = MPVCore::instance();
        int data = (int)(value * 200 - 100);
        cell->setDetailText(std::to_string(data));
        switch (item) {
        case Equalizer::BRIGHTNESS:
            mpv.setInt("brightness", data);
            break;
        case Equalizer::CONTRAST:
            mpv.setInt("contrast", data);
            break;
        case Equalizer::SATURATION:
            mpv.setInt("saturation", data);
            break;
        case Equalizer::GAMMA:
            mpv.setInt("gamma", data);
            break;
        case Equalizer::HUE:
            mpv.setInt("hue", data);
            break;
        default:;
        }
    });
    registerHideBackground(cell->getDefaultFocus());
}

void PlayerSetting::registerHideBackground(brls::View* view) {
    view->getFocusEvent()->subscribe([this](...) { this->setBackgroundColor(nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f)); });
    view->getFocusLostEvent()->subscribe(
        [this](...) { this->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop")); });
}