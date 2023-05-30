#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "{}/Users/{}/Views";
const std::string apiUserLibrary = "{}/Users/{}/Items?{}";
const std::string apiUserItem = "{}/Users/{}/Items/{}";
const std::string apiShowSeanon = "{}/Shows/{}/Seasons?{}";
const std::string apiShowEpisodes = "{}/Shows/{}/Episodes?{}";
const std::string apiPrimaryImage = "{}/Items/{}/Images/Primary?{}";
const std::string apiLogoImage = "{}/Items/{}/Images/Logo?{}";
const std::string apiPlayback = "{}/Items/{}/PlaybackInfo?{}";
const std::string apiStream = "{}/Videos/{}/stream.{}?{}";

const std::string imageTypePrimary = "Primary";
const std::string imageTypeLogo = "Logo";

const std::string mediaTypeFolder = "CollectionFolder";
const std::string mediaTypeSeries = "Series";
const std::string mediaTypeSeason = "Season";
const std::string mediaTypeEpisode = "Episode";

struct MediaItem {
    std::string Id;
    std::string Name;
    std::string Type;
    std::map<std::string, std::string> ImageTags;
    double PrimaryImageAspectRatio = 1.0f;
    bool IsFolder = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaItem, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder);

struct MediaSeries : public MediaItem {
    long ProductionYear = 0;
    float CommunityRating = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MediaSeries, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, ProductionYear, CommunityRating);

struct MediaSeason : public MediaItem {
    long ProductionYear = 0;
    std::string SeriesName;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MediaSeason, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, ProductionYear, SeriesName);

struct MediaStream {
    std::string Codec;
    std::string DisplayTitle;
    std::string Type;
    long Index;
    bool IsDefault;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaStream, Codec, DisplayTitle, Type, Index, IsDefault);

struct MediaSource {
    std::string Id;
    std::string Name;
    std::string Container;
    int DefaultAudioStreamIndex;
    int DefaultSubtitleStreamIndex;
    std::string ETag;
    time_t RunTimeTicks;
    std::vector<MediaStream> MediaStreams;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaSource, Id, Name, Container, DefaultAudioStreamIndex,
    DefaultSubtitleStreamIndex, ETag, RunTimeTicks, MediaStreams);

struct PlaybackResult {
    std::vector<MediaSource> MediaSources;
    std::string PlaySessionId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlaybackResult, MediaSources, PlaySessionId);

struct MediaEpisode : public MediaItem {
    std::string Overview;
    long IndexNumber;
    std::vector<MediaSource> MediaSources;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MediaEpisode, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, Overview, IndexNumber, MediaSources);

template <typename T>
struct MediaQueryResult {
    std::vector<T> Items;
    long TotalRecordCount = 0;
    long StartIndex = 0;
};

template <typename T>
inline void to_json(nlohmann::json& nlohmann_json_j, const MediaQueryResult<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, Items, TotalRecordCount, StartIndex))
}

template <typename T>
inline void from_json(const nlohmann::json& nlohmann_json_j, MediaQueryResult<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, Items, TotalRecordCount, StartIndex))
}

}  // namespace jellyfin