#include "utils/config.hpp"
#include "view/button_close.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/track_selection.hpp"
#include <sstream>
#endif

using namespace brls::literals;

#ifdef PS5_NATIVE_GPU
namespace {
using track_selection::Kind;

std::vector<track_selection::MpvTrack> playerTracks() {
    auto& mpv = MPVCore::instance();
    std::vector<track_selection::MpvTrack> tracks;
    for (int64_t n = 0, count = mpv.getInt("track-list/count"); n < count; ++n) {
        const std::string prefix = fmt::format("track-list/{}/", n);
        const std::string type = mpv.getString(prefix + "type");
        if (type != "audio" && type != "sub") continue;
        const std::string external = mpv.getString(prefix + "external");
        auto& track = tracks.emplace_back(track_selection::MpvTrack{type == "audio" ? Kind::Audio : Kind::Subtitle,
            mpv.getInt(prefix + "id", -1), mpv.getInt(prefix + "ff-index", -1),
            external == "yes" || external == "true", mpv.getString(prefix + "external-filename")});
        track.sourceId = mpv.getInt(prefix + "src-id", -1);
        track.title = mpv.getString(prefix + "title");
        track.language = mpv.getString(prefix + "lang");
        track.isDefault = mpv.getString(prefix + "default") == "yes";
        track.forced = mpv.getString(prefix + "forced") == "yes";
        if (track.title.empty()) track.title = type == "sub" ? "Subtitle" : "Audio";
        if (!track.language.empty()) track.title += " - " + track.language;
        if (track.forced) track.title += " - Forced";
    }
    return tracks;
}

std::vector<track_selection::ServerTrack> serverTracks(const jellyfin::Source* source) {
    std::vector<track_selection::ServerTrack> streams;
    if (!source) return streams;
    for (const auto& stream : source->MediaStreams) {
        if (stream.Type != jellyfin::streamTypeAudio && stream.Type != jellyfin::streamTypeSubtitle) continue;
        std::string title = stream.DisplayTitle;
        if (title.empty()) {
            title = stream.Title.empty() ? stream.Type : stream.Title;
            if (!stream.Language.empty()) title += " - " + stream.Language;
            if (stream.IsForced) title += " - Forced";
        }
        streams.push_back({stream.Type == jellyfin::streamTypeAudio ? Kind::Audio : Kind::Subtitle,
            stream.Index, stream.IsExternal,
            stream.DeliveryUrl.empty() || stream.DeliveryMethod == "Encode" ? "" : PlayerSetting::subtitleUrl(stream.DeliveryUrl),
            title,
            stream.Language, stream.IsDefault, stream.IsForced, stream.DeliveryMethod == "Encode"});
    }
    return streams;
}

bool originalFfmpegIndices(const jellyfin::Source* source, bool directPlay) {
    // mpv documents ff-index as potentially different for its builtin demuxers.
    // Remote sources and server transcoding may also change the container.
    return source && directPlay && !source->IsRemote && MPVCore::instance().getString("current-demuxer") == "lavf";
}

track_selection::ServerDefault defaultFor(const jellyfin::Source& source, Kind kind) {
    return kind == Kind::Audio
        ? track_selection::ServerDefault{source.HasDefaultAudioStreamIndex, source.DefaultAudioStreamIndex}
        : track_selection::ServerDefault{source.HasDefaultSubtitleStreamIndex, source.DefaultSubtitleStreamIndex};
}

track_selection::SourceIdentity sourceIdentity(const jellyfin::Source& source, bool directPlay) {
    return {source.Id, source.ETag, MPVCore::instance().getString("current-demuxer"), directPlay && !source.IsRemote};
}

std::vector<std::string> preferredSubtitleLanguages() {
    std::vector<std::string> result;
    std::stringstream list(MPVCore::instance().getString("slang"));
    for (std::string value; std::getline(list, value, ',');) if (!value.empty()) result.push_back(value);
    return result;
}
} // namespace

std::string PlayerSetting::subtitleUrl(const std::string& url) {
    if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) return url;
    return AppConfig::instance().getUrl() + url;
}

void PlayerSetting::resetTrackSelections() {
    ++trackLoadGeneration;
    subtitleSelection = {};
    audioSelection = {};
}

std::string PlayerSetting::trackLoadOptions(const jellyfin::Source& source) {
    ++trackLoadGeneration;
    audioSelection.beginLoad(source.Id, source.ETag);
    subtitleSelection.beginLoad(source.Id, source.ETag);
    const auto decision = track_selection::decide(Kind::Subtitle, subtitleSelection, defaultFor(source, Kind::Subtitle));
    bool automatic = decision.mode == track_selection::Decision::Mode::Automatic;
    if (subtitleSelection.intent == track_selection::Selection::Intent::Default &&
        decision.mode == track_selection::Decision::Mode::Server) {
        const auto streams = serverTracks(&source);
        const auto* stream = track_selection::serverTrackFor(Kind::Subtitle, *decision.serverIndex, streams);
        // Builtin demuxers cannot always map a server embedded default. Preserve
        // mpv's working language/default selection until a mapping is verified.
        automatic = !stream || stream->externalSource.empty();
    }
    // Explicit Off/choices and externally delivered defaults cannot briefly
    // display an unrelated track while the intended subtitle is being loaded.
    return std::string(",aid=auto,sid=") + (automatic ? "auto" : "no");
}

void PlayerSetting::loadTracks(const jellyfin::Source& source, bool directPlay) {
    auto& mpv = MPVCore::instance();
    const auto tracks = playerTracks();
    const auto streams = serverTracks(&source);
    const auto identity = sourceIdentity(source, directPlay);
    const bool original = originalFfmpegIndices(&source, directPlay);
    auto apply = [&](track_selection::Selection& selection, Kind kind, const char* property) {
        const auto decision = track_selection::decide(kind, selection, defaultFor(source, kind));
        if (decision.mode == track_selection::Decision::Mode::Off) {
            mpv.command("set", property, "no");
            return;
        }
        std::optional<int64_t> id;
        const track_selection::ServerTrack* stream = nullptr;
        if (decision.mode == track_selection::Decision::Mode::Server) {
            stream = track_selection::serverTrackFor(kind, *decision.serverIndex, streams);
            if (kind == Kind::Subtitle && !directPlay && stream && stream->encoded) return;
            id = track_selection::mpvIdFor(kind, *decision.serverIndex, tracks, streams, original);
            // A single audio stream in a server-produced transcode corresponds
            // to the authoritative delivered default, not its new ff-index.
            if (!id && !directPlay && kind == Kind::Audio && stream &&
                source.DefaultAudioStreamIndex == decision.serverIndex) {
                id = track_selection::singleTrackId(kind, tracks);
            }
        } else if (decision.mode == track_selection::Decision::Mode::Local) {
            id = track_selection::localMpvIdFor(kind, selection, identity, tracks);
        } else if (kind == Kind::Subtitle && mpv.getInt("sid") <= 0) {
            auto fallback = track_selection::fallbackExternal(streams, preferredSubtitleLanguages(),
                mpv.getString("current-tracks/audio/lang"), MPVCore::SUBS_FALLBACK);
            if (fallback) stream = track_selection::serverTrackFor(kind, *fallback, streams);
        }
        if (id) {
            selection.mpvId = *id;
            mpv.setInt(property, *id);
        } else if (kind == Kind::Subtitle && stream && !stream->externalSource.empty()) {
            // Only the intended external track is loaded. Other choices remain
            // in the settings catalog. cached selects an existing exact URL,
            // avoiding duplicate tracks and asynchronous add/query races.
            mpv.command("sub-add", stream->externalSource.c_str(), "cached", stream->title.c_str(), stream->language.c_str());
        } else if (decision.mode == track_selection::Decision::Mode::Server ||
                   decision.mode == track_selection::Decision::Mode::Local) {
            brls::Logger::warning("player: {} preference has no verified mapping in this source", property);
        }
    };
    apply(audioSelection, Kind::Audio, "aid");
    apply(subtitleSelection, Kind::Subtitle, "sid");
}

void PlayerSetting::selectTrackChoice(const track_selection::Choice& choice, Kind kind,
    const std::vector<track_selection::MpvTrack>& tracks, const track_selection::SourceIdentity& source,
    bool subtitleBurnedIn, uint64_t expectedGeneration) {
    // A settings sheet opened before a quality reload or episode change holds
    // old mpv IDs. Do not apply its callback to a different loaded generation.
    if (expectedGeneration != trackLoadGeneration) return;
    auto& selection = kind == Kind::Audio ? audioSelection : subtitleSelection;
    auto& mpv = MPVCore::instance();
    const char* property = kind == Kind::Audio ? "aid" : "sid";
    if (choice.off) {
        selection.disable();
        mpv.command("set", property, "no");
        if (kind == Kind::Subtitle && subtitleBurnedIn) mpv.getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
    } else if (choice.mpvId) {
        for (const auto& track : tracks) if (track.kind == kind && track.id == *choice.mpvId) {
            selection.selectLocal(track, choice.serverIndex, source);
            if (kind == Kind::Subtitle && subtitleBurnedIn)
                mpv.getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            else
                mpv.setInt(property, track.id);
            break;
        }
    } else if (choice.serverIndex) {
        selection.selectServer(*choice.serverIndex, source);
        if (kind == Kind::Subtitle && !subtitleBurnedIn && !choice.externalSource.empty())
            mpv.command("sub-add", choice.externalSource.c_str(), "cached", choice.title.c_str(), choice.language.c_str());
        else
            mpv.getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
    }
}

void PlayerSetting::restoreTrackSelections(const jellyfin::Source& source, bool directPlay) {
    auto& mpv = MPVCore::instance();
    if (subtitleSelection.isDisabled()) mpv.command("set", "sid", "no");
    // Ordinary startup and subtitle-off need no track-list queries. Inspect
    // fresh IDs only when restoring an explicit, mapped choice after reload.
    if (!audioSelection.serverIndex && !subtitleSelection.serverIndex) return;
    const auto tracks = playerTracks();
    const auto streams = serverTracks(&source);
    const bool original = originalFfmpegIndices(&source, directPlay);
    auto restore = [&](track_selection::Selection& selection, Kind kind, const char* property) {
        if (selection.serverIndex) {
            auto id = track_selection::mpvIdFor(kind, *selection.serverIndex, tracks, streams, original);
            if (id) {
                selection.mpvId = *id;
                mpv.setInt(property, *id);
            }
            // A transcoded audio track is selected by the server request. An
            // external subtitle may not exist yet; sub-add selects that stream.
        }
    };
    restore(audioSelection, Kind::Audio, "aid");
    restore(subtitleSelection, Kind::Subtitle, "sid");
}

PlayerSetting::PlayerSetting(const jellyfin::Source* src, bool directPlay) {
    this->inflateFromXMLRes("xml/ps5/player_setting.xml");
#else
PlayerSetting::PlayerSetting(const jellyfin::Source* src) {
    this->inflateFromXMLRes("xml/view/player_setting.xml");
#endif
    brls::Logger::debug("PlayerSetting: create");
    this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    this->subtitleTrack->detail->setVisibility(brls::Visibility::GONE);

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

#ifdef PS5_NATIVE_GPU
    const auto tracks = playerTracks();
    const auto streams = serverTracks(src);
    const bool original = originalFfmpegIndices(src, directPlay);
    const auto identity = src ? sourceIdentity(*src, directPlay) : track_selection::SourceIdentity{};
    const auto generation = trackLoadGeneration;
    const bool subtitleBurnedIn = src && !directPlay && std::any_of(src->MediaStreams.begin(), src->MediaStreams.end(),
        [&](const jellyfin::Stream& stream) {
            return stream.Type == jellyfin::streamTypeSubtitle && stream.DeliveryMethod == "Encode" &&
                src->DefaultSubtitleStreamIndex == stream.Index;
        });
    auto setupTracks = [&](brls::SelectorCell* cell, Kind kind, const std::string& title) {
        auto choices = track_selection::choicesFor(kind, tracks, streams, original, directPlay,
            src ? src->DefaultAudioStreamIndex : std::nullopt);
        if (choices.size() <= 1) { cell->setVisibility(brls::Visibility::GONE); return; }
        std::vector<std::string> labels;
        int selected = 0;
        const auto current = mpv.getInt(kind == Kind::Audio ? "aid" : "sid", -1);
        for (size_t i = 0; i < choices.size(); ++i) {
            const auto& choice = choices[i];
            labels.push_back(choice.off ? "main/player/none"_i18n : choice.title.empty()
                ? fmt::format("{} {}", kind == Kind::Audio ? "Audio" : "Subtitle", choice.serverIndex.value_or(choice.mpvId.value_or(0)))
                : choice.title);
            if (choice.mpvId == current) selected = static_cast<int>(i);
            else if (kind == Kind::Subtitle && subtitleBurnedIn && choice.serverIndex == src->DefaultSubtitleStreamIndex)
                selected = static_cast<int>(i);
#else
    std::vector<std::string> audioTrack, audioSource;
    std::vector<int> audioStream;
    std::vector<std::string> subTrack = {"main/player/none"_i18n};
    std::vector<std::string> subSource = {"main/player/none"_i18n};
    std::vector<int> subStream = {0};

    int64_t count = mpv.getInt("track-list/count");
    for (int64_t n = 0; n < count; n++) {
        std::string type  = mpv.getString(fmt::format("track-list/{}/type", n));
        std::string title = mpv.getString(fmt::format("track-list/{}/title", n));
        std::string lang  = mpv.getString(fmt::format("track-list/{}/lang", n));
        if (!title.empty() && !lang.empty())
            title = fmt::format("{} - {}", title, lang);
        else if (title.empty())
            title = lang;
        if (title.empty()) title = fmt::format("{} track {}", type, n);
        if (type == "sub")
            subTrack.push_back(title);
        else if (type == "audio")
            audioTrack.push_back(title);
    }

    if (src != nullptr) {
        for (auto& s : src->MediaStreams) {
            if (s.Type == jellyfin::streamTypeAudio) {
                audioSource.push_back(s.DisplayTitle);
                audioStream.push_back(s.Index);
            } else if (s.Type == jellyfin::streamTypeSubtitle) {
                subSource.push_back(s.DisplayTitle);
                subStream.push_back(s.Index);
            }
#endif
        }
#ifdef PS5_NATIVE_GPU
        cell->init(title, labels, selected, [choices, tracks, identity, kind, subtitleBurnedIn, generation](int index) {
            if (index < 0 || static_cast<size_t>(index) >= choices.size()) return;
            selectTrackChoice(choices[index], kind, tracks, identity, subtitleBurnedIn, generation);
#else
    }
    // 字幕选择
    if (subTrack.size() > 1) {
        int64_t value = mpv.getInt("sid");
        this->subtitleTrack->init("main/player/subtitle"_i18n, subTrack, value, [&mpv](int selected) {
            selectedSubtitle = selected;
            mpv.setInt("sid", selected);
#endif
        });
#ifdef PS5_NATIVE_GPU
    };
    setupTracks(this->subtitleTrack, Kind::Subtitle, "main/player/subtitle"_i18n);
    setupTracks(this->audioTrack, Kind::Audio, "main/player/audio"_i18n);
#else
    } else if (subSource.size() > 1) {
        int value = 0;
        for (size_t i = 0; i < subStream.size(); i++)
            if (subStream[i] == selectedSubtitle) value = i;
        this->subtitleTrack->init("main/player/subtitle"_i18n, subSource, value, [subStream](int selected) {
            selectedSubtitle = subStream[selected];
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
        });
    } else {
        this->subtitleTrack->setVisibility(brls::Visibility::GONE);
    }
    // 音轨选择
    if (audioTrack.size() > 1) {
        int64_t value = mpv.getInt("aid", 1) - 1;
        this->audioTrack->init("main/player/audio"_i18n, audioTrack, value, [&mpv](int selected) {
            selectedAudio = selected + 1;
            mpv.setInt("aid", selectedAudio);
        });
        this->audioTrack->detail->setVisibility(brls::Visibility::GONE);
    } else if (audioSource.size() > 1) {
        int value = 0;
        for (size_t i = 0; i < audioStream.size(); i++)
            if (audioStream[i] == selectedAudio) value = i;
        this->audioTrack->init("main/player/audio"_i18n, audioSource, value, [audioStream](int selected) {
            selectedAudio = audioStream[selected];
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
        });
    } else {
        this->audioTrack->setVisibility(brls::Visibility::GONE);
    }
#endif

    auto& conf = AppConfig::instance();
#ifdef PS5_NATIVE_GPU

    // Use text-only style properties: global sub-scale/sub-pos also move and
    // resize authored ASS. Neither opening this menu nor Default changes the
    // user's mpv.conf style unless a saved appearance override is being reset.
    std::vector<std::string> sizes{"main/setting/subtitle/default"_i18n};
    for (size_t i = 1; i < subtitle_appearance::sizes.size(); ++i)
        sizes.push_back(fmt::format("{}%", subtitle_appearance::sizes[i]));
    this->subtitleSize->init("main/setting/subtitle/size"_i18n, sizes,
        subtitle_appearance::indexOf(subtitle_appearance::sizes, conf.getItem(AppConfig::PS5_SUBTITLE_SIZE, 0)),
        [](int selected) {
            if (selected < 0 || static_cast<size_t>(selected) >= subtitle_appearance::sizes.size()) return;
            AppConfig::instance().setItem(AppConfig::PS5_SUBTITLE_SIZE, subtitle_appearance::sizes[selected]);
            MPVCore::instance().applySubtitlePreferences();
        });
    this->subtitleMargin->init("main/setting/subtitle/position"_i18n,
        {"main/setting/subtitle/default"_i18n, "main/setting/subtitle/bottom"_i18n,
            "main/setting/subtitle/raised_small"_i18n, "main/setting/subtitle/raised"_i18n,
            "main/setting/subtitle/high"_i18n, "main/setting/subtitle/higher"_i18n},
        subtitle_appearance::indexOf(subtitle_appearance::margins, conf.getItem(AppConfig::PS5_SUBTITLE_MARGIN, -1)),
        [](int selected) {
            if (selected < 0 || static_cast<size_t>(selected) >= subtitle_appearance::margins.size()) return;
            AppConfig::instance().setItem(AppConfig::PS5_SUBTITLE_MARGIN, subtitle_appearance::margins[selected]);
            MPVCore::instance().applySubtitlePreferences();
        });
#endif

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

    btnBottomBar->init(
        "main/setting/playback/bottom_bar"_i18n, conf.getItem(AppConfig::PLAYER_BOTTOM_BAR, true), [&conf](bool value) {
            MPVCore::BOTTOM_BAR = value;
            conf.setItem(AppConfig::PLAYER_BOTTOM_BAR, value);
        });

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

    /// Subsync
    double subDelay = mpv.getDouble("sub-delay");
    btnSubsync->title->setMarginRight(0);
    btnSubsync->slider->setMarginRight(0);
    btnSubsync->slider->setPointerSize(20);
    btnSubsync->setDetailText(fmt::format("{:.1f}", subDelay));
    btnSubsync->init("main/setting/playback/subsync"_i18n, (subDelay + 10) * 0.05f, [this](float value) {
        float data = value * 20 - 10.f;
        MPVCore::instance().setDouble("sub-delay", data);
        btnSubsync->setDetailText(fmt::format("{:.1f}", data));
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
#ifdef PS5_NATIVE_GPU
}
#else
}
#endif
