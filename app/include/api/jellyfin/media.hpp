#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "{}/Users/{}/Views";
const std::string apiUserLibrary = "{}/Users/{}/Items?{}";
const std::string apiUserItem = "{}/Users/{}/Items/{}";
const std::string apiShowSeanon = "{}/Shows/{}/Seasons?{}";
const std::string apiShowEpisodes = "{}/Shows/{}/Episodes?{}";
const std::string apiPrimaryImage = "{}/Items/{}/Images/Primary?{}";

const std::string imageTypePrimary = "Primary";
const std::string imageTypeLogo = "Logo";

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaSeries, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, ProductionYear, CommunityRating);

struct MediaSeason : public MediaItem {
    long ProductionYear = 0;
    std::string SeriesName;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaSeason, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, ProductionYear, SeriesName);


struct MediaEpisode : public MediaItem {
    std::string Overview;
    long IndexNumber;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaEpisode, Id, Name, Type, ImageTags, PrimaryImageAspectRatio, IsFolder, Overview, IndexNumber);

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