//
// Created by fang on 2023/3/4.
//

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#ifdef PS5_NATIVE_GPU
#include <utils/track_selection.hpp>
#endif

class ButtonClose;

class PlayerSetting : public brls::Box {
public:
#ifdef PS5_NATIVE_GPU
    PlayerSetting(const jellyfin::Source* src = nullptr, bool directPlay = false);
#else
    PlayerSetting(const jellyfin::Source* src = nullptr);
#endif
    ~PlayerSetting() override;

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->settings->getDefaultFocus(); }

#ifdef PS5_NATIVE_GPU
    inline static track_selection::Selection subtitleSelection;
    inline static track_selection::Selection audioSelection;
    static void resetTrackSelections();
    static void restoreTrackSelections(const jellyfin::Source& source, bool directPlay);
    static std::string subtitleUrl(const std::string& deliveryUrl);
    inline static uint64_t trackLoadGeneration = 0;
    static std::string trackLoadOptions(const jellyfin::Source& source);
    static void loadTracks(const jellyfin::Source& source, bool directPlay);
    static void selectTrackChoice(const track_selection::Choice& choice, track_selection::Kind kind,
        const std::vector<track_selection::MpvTrack>& tracks, const track_selection::SourceIdentity& source,
        bool subtitleBurnedIn, uint64_t expectedGeneration = trackLoadGeneration);
#else
    inline static int selectedSubtitle = 0;
    inline static int selectedAudio = 0;
#endif

    enum class Equalizer {
        BRIGHTNESS,
        CONTRAST,
        SATURATION,
        HUE,
        GAMMA,
    };

private:
    BRLS_BIND(brls::ScrollingFrame, settings, "player/settings");
    BRLS_BIND(brls::Box, cancel, "player/cancel");

    BRLS_BIND(brls::SelectorCell, subtitleTrack, "setting/track/subtitle");
#ifdef PS5_NATIVE_GPU
    BRLS_BIND(brls::Box, subtitleAppearance, "setting/subtitle/appearance");
    BRLS_BIND(brls::SelectorCell, subtitleSize, "setting/subtitle/size");
    BRLS_BIND(brls::SelectorCell, subtitleMargin, "setting/subtitle/margin");
#endif
    BRLS_BIND(brls::SelectorCell, audioTrack, "setting/track/audio");
    BRLS_BIND(brls::BooleanCell, btnBottomBar, "setting/player/bottom_bar");
    BRLS_BIND(brls::BooleanCell, btnOSDOnToggle, "setting/player/osd_on_toggle");
    BRLS_BIND(brls::BooleanCell, btnFullscreen, "setting/fullscreen");
    BRLS_BIND(brls::BooleanCell, btnAlwaysOnTop, "setting/always_on_top");
    BRLS_BIND(brls::SelectorCell, btnVideoMirror, "setting/video/mirror");
    BRLS_BIND(brls::SelectorCell, btnVideoRotation, "setting/video/rotation");
    BRLS_BIND(brls::SelectorCell, btnVideoAspect, "setting/video/aspect");
    BRLS_BIND(brls::SliderCell, btnSubsync, "setting/video/subsync");
    // equalizer setting
    BRLS_BIND(brls::RadioCell, btnEqualizerReset, "setting/equalizer/reset");
    BRLS_BIND(brls::SliderCell, btnEqualizerBrightness, "setting/equalizer/brightness");
    BRLS_BIND(brls::SliderCell, btnEqualizerContrast, "setting/equalizer/contrast");
    BRLS_BIND(brls::SliderCell, btnEqualizerSaturation, "setting/equalizer/saturation");
    BRLS_BIND(brls::SliderCell, btnEqualizerGamma, "setting/equalizer/gamma");
    BRLS_BIND(brls::SliderCell, btnEqualizerHue, "setting/equalizer/hue");

    void setupEqualizer(brls::SliderCell* cell, const std::string& title, Equalizer item, double initValue);

    void registerHideBackground(brls::View* view);
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
