#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace track_selection {
enum class Kind { Audio, Subtitle };

struct MpvTrack {
    Kind kind;
    int64_t id;
    int64_t ffIndex = -1;
    bool external = false;
    std::string externalSource;
    int64_t sourceId = -1;
    std::string title = {};
    std::string language = {};
    bool isDefault = false;
    bool forced = false;
};

struct ServerTrack {
    Kind kind;
    int64_t index;
    bool external = false;
    std::string externalSource;
    std::string title = {};
    std::string language = {};
    bool isDefault = false;
    bool forced = false;
    bool encoded = false;
};

struct SourceIdentity {
    std::string id;
    std::string tag;
    std::string demuxer;
    bool original = false;
};

// src-id is a container identity, not a Jellyfin/FFmpeg stream index. It can
// preserve an unmapped local choice only within the same original source.
struct LocalIdentity {
    SourceIdentity source;
    int64_t sourceId = -1;
    std::string externalSource;
};

// FFmpeg indices describe the original container only on a qualified direct
// path. Transcoding/remuxing may reorder them. External files instead require
// an exact, unique identity; titles, languages and URL basenames are not IDs.
inline std::optional<int64_t> serverIndexFor(const MpvTrack& track,
    const std::vector<ServerTrack>& streams, bool originalFfmpegIndices) {
    std::optional<int64_t> result;
    for (const auto& stream : streams) {
        if (stream.kind != track.kind || stream.index < 0) continue;
        bool matches = track.external
            ? !track.externalSource.empty() && track.externalSource == stream.externalSource
            : originalFfmpegIndices && !stream.external && track.ffIndex >= 0 && track.ffIndex == stream.index;
        if (!matches) continue;
        if (result) return std::nullopt;
        result = stream.index;
    }
    return result;
}

inline std::optional<int64_t> mpvIdFor(Kind kind, int64_t serverIndex,
    const std::vector<MpvTrack>& tracks, const std::vector<ServerTrack>& streams,
    bool originalFfmpegIndices) {
    std::optional<int64_t> result;
    for (const auto& track : tracks) {
        if (track.kind != kind || track.id <= 0) continue;
        auto mapped = serverIndexFor(track, streams, originalFfmpegIndices);
        if (!mapped || *mapped != serverIndex) continue;
        if (result) return std::nullopt;
        result = track.id;
    }
    return result;
}

struct Selection {
    enum class Intent { Default, Selected, Disabled };
    Intent intent = Intent::Default;
    std::optional<int64_t> serverIndex;
    std::optional<int64_t> mpvId;
    std::optional<LocalIdentity> localIdentity;
    std::optional<SourceIdentity> sourceScope;

    void selectLocal(int64_t id, std::optional<int64_t> mappedIndex) {
        intent = Intent::Selected;
        mpvId = id;
        serverIndex = mappedIndex;
        localIdentity.reset();
        sourceScope.reset();
    }
    void selectLocal(const MpvTrack& track, std::optional<int64_t> mappedIndex, const SourceIdentity& source) {
        selectLocal(track.id, mappedIndex);
        sourceScope = source;
        if (!track.externalSource.empty() || (source.original && track.sourceId >= 0))
            localIdentity = LocalIdentity{source, track.sourceId, track.externalSource};
    }
    void selectServer(int64_t index) {
        intent = Intent::Selected;
        serverIndex = index;
        mpvId.reset();
        localIdentity.reset();
        sourceScope.reset();
    }
    void selectServer(int64_t index, const SourceIdentity& source) {
        selectServer(index);
        sourceScope = source;
    }
    void disable() {
        intent = Intent::Disabled;
        serverIndex.reset();
        mpvId = 0;
        localIdentity.reset();
        sourceScope.reset();
    }
    void beginLoad() { mpvId.reset(); }
    void beginLoad(const std::string& sourceId, const std::string& sourceTag) {
        if (sourceScope && (sourceScope->id != sourceId || sourceScope->tag != sourceTag)) *this = {};
        beginLoad();
    }
    std::optional<int64_t> requestIndex() const {
        if (intent == Intent::Disabled) return -1;
        if (intent == Intent::Selected && serverIndex && *serverIndex >= 0) return serverIndex;
        return std::nullopt;
    }
    bool isDisabled() const { return intent == Intent::Disabled; }
    bool selectExternal(int64_t index, bool fallback) const {
        return intent == Intent::Default ? fallback
            : intent == Intent::Selected && serverIndex && *serverIndex == index;
    }
};

struct ServerDefault {
    bool present = false;
    std::optional<int64_t> index;
};

struct Decision {
    enum class Mode { Automatic, Off, Server, Local };
    Mode mode = Mode::Automatic;
    std::optional<int64_t> serverIndex;
};

inline Decision decide(Kind kind, const Selection& choice, const ServerDefault& server) {
    if (choice.isDisabled()) return {Decision::Mode::Off, std::nullopt};
    if (choice.intent == Selection::Intent::Selected) {
        if (choice.serverIndex && *choice.serverIndex >= 0)
            return {Decision::Mode::Server, choice.serverIndex};
        return {Decision::Mode::Local, std::nullopt};
    }
    if (server.index && *server.index >= 0) return {Decision::Mode::Server, server.index};
    // Jellyfin's nullable subtitle default also represents user policy None.
    // An omitted key is unknown; never turn it into a choice of stream zero.
    if (kind == Kind::Subtitle && server.present) return {Decision::Mode::Off, std::nullopt};
    return {};
}

inline std::optional<int64_t> localMpvIdFor(Kind kind, const Selection& choice,
    const SourceIdentity& source, const std::vector<MpvTrack>& tracks) {
    if (!choice.localIdentity || source.id.empty()) return std::nullopt;
    const auto& identity = *choice.localIdentity;
    if (source.id != identity.source.id || source.tag != identity.source.tag) return std::nullopt;
    std::optional<int64_t> result;
    for (const auto& track : tracks) {
        if (track.kind != kind || track.id <= 0) continue;
        const bool match = !identity.externalSource.empty()
            ? track.external && track.externalSource == identity.externalSource
            : source.original && identity.source.original && !track.external &&
                source.demuxer == identity.source.demuxer && track.sourceId >= 0 &&
                track.sourceId == identity.sourceId;
        if (!match) continue;
        if (result) return std::nullopt;
        result = track.id;
    }
    return result;
}

inline const ServerTrack* serverTrackFor(Kind kind, int64_t index, const std::vector<ServerTrack>& streams) {
    const ServerTrack* result = nullptr;
    for (const auto& stream : streams) {
        if (stream.kind != kind || stream.index != index || index < 0) continue;
        if (result) return nullptr;
        result = &stream;
    }
    return result;
}

inline std::optional<int64_t> singleTrackId(Kind kind, const std::vector<MpvTrack>& tracks) {
    std::optional<int64_t> result;
    for (const auto& track : tracks) {
        if (track.kind != kind || track.id <= 0) continue;
        if (result) return std::nullopt;
        result = track.id;
    }
    return result;
}

inline std::string normalizedLanguage(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '_') c = '-';
    }
    return value;
}

// Only used when PlaybackInfo omitted its subtitle default AND mpv selected
// no embedded track. Server policy, explicit Off and explicit choices bypass it.
inline std::optional<int64_t> fallbackExternal(const std::vector<ServerTrack>& streams,
    const std::vector<std::string>& preferredLanguages, const std::string& audioLanguage, bool allowFallback) {
    std::optional<int64_t> result;
    int best = -1;
    for (const auto& stream : streams) {
        if (stream.kind != Kind::Subtitle || stream.index < 0 || stream.externalSource.empty()) continue;
        const auto language = normalizedLanguage(stream.language);
        int rank = 0;
        for (size_t i = 0; i < preferredLanguages.size(); ++i) {
            if (!language.empty() && language == normalizedLanguage(preferredLanguages[i])) {
                rank = 1000 - static_cast<int>(std::min<size_t>(i, 99)) * 4;
                break;
            }
        }
        const bool forcedLanguage = stream.forced && !language.empty() &&
            language == normalizedLanguage(audioLanguage);
        if (!allowFallback && !rank && !forcedLanguage && !stream.isDefault) continue;
        rank += forcedLanguage ? 3 : stream.isDefault ? 2 : stream.forced ? 1 : 0;
        // Equal scores keep the first server-listed candidate; exactly one is
        // chosen. This is preference order, never an mpv/server ID mapping.
        if (rank > best) { best = rank; result = stream.index; }
    }
    return result;
}

struct Choice {
    std::string title;
    std::optional<int64_t> mpvId;
    std::optional<int64_t> serverIndex;
    std::string externalSource;
    std::string language;
    bool off = false;
};

inline std::vector<Choice> choicesFor(Kind kind, const std::vector<MpvTrack>& tracks,
    const std::vector<ServerTrack>& streams, bool originalFfmpegIndices, bool directPlay,
    std::optional<int64_t> deliveredAudioIndex = std::nullopt) {
    std::vector<Choice> result;
    if (kind == Kind::Subtitle) result.push_back({"", std::nullopt, std::nullopt, "", "", true});
    for (const auto& track : tracks) {
        if (track.kind != kind || track.id <= 0) continue;
        auto mapped = serverIndexFor(track, streams, originalFfmpegIndices);
        if (!mapped && !directPlay && kind == Kind::Audio && deliveredAudioIndex &&
            serverTrackFor(kind, *deliveredAudioIndex, streams) && singleTrackId(kind, tracks) == track.id)
            mapped = deliveredAudioIndex;
        result.push_back({track.title, track.id, mapped,
            track.externalSource, track.language});
    }
    for (const auto& stream : streams) {
        if (stream.kind != kind || stream.index < 0) continue;
        if (std::any_of(result.begin(), result.end(), [&](const Choice& value) {
                return value.serverIndex == stream.index;
            })) continue;
        // Direct embedded tracks are already offered with their exact local
        // IDs. External delivery remains available even beside embedded tracks.
        if (directPlay && !stream.external && (kind == Kind::Audio || stream.externalSource.empty())) continue;
        result.push_back({stream.title, std::nullopt, stream.index, stream.externalSource, stream.language});
    }
    return result;
}

inline int indexOf(const std::vector<int64_t>& ids, int64_t id) {
    auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
}

inline bool resolve(const std::vector<int64_t>& ids, int index, int64_t& id) {
    if (index < 0 || static_cast<size_t>(index) >= ids.size()) return false;
    id = ids[static_cast<size_t>(index)];
    return true;
}
} // namespace track_selection
