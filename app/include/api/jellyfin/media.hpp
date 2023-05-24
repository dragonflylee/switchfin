#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "/Users/{}/Views";
const std::string apiUserLibrary = "/Users/{}/Items?{}";
const std::string apiPrimaryImage = "/Items/{}/Images/Primary?{}";

const std::string imageTypePrimary = "Primary";

struct MediaItem {
    std::string Id;
    std::string Name;
    std::string Type;
    std::map<std::string, std::string> ImageTags;
    bool IsFolder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaItem, Id, Name, Type, ImageTags, IsFolder);

struct MediaSeries : public MediaItem {
    long ProductionYear;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaSeries, Id, Name, Type, ImageTags, IsFolder, ProductionYear);

template <typename Item = MediaItem>
struct MediaQueryResult {
    std::vector<Item> Items;
    long TotalRecordCount;
    long StartIndex;
};

template <typename Item>
inline void to_json(nlohmann::json& nlohmann_json_j, const MediaQueryResult<Item>& nlohmann_json_t) { 
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, Items, TotalRecordCount, StartIndex)) 
}

template <typename Item>
inline void from_json(const nlohmann::json& nlohmann_json_j, MediaQueryResult<Item>& nlohmann_json_t) { 
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, Items, TotalRecordCount, StartIndex))
}

}