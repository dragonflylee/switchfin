#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "/Users/{}/Views";
const std::string apiUserLibrary = "/Users/{}/Items?{}";
const std::string apiUserItem = "/Users/{}/Items/{}";
const std::string apiShowSeanon = "/Shows/{}/Seasons?{}";
const std::string apiPrimaryImage = "/Items/{}/Images/Primary?{}";

const std::string imageTypePrimary = "Primary";

struct MediaItem {
    std::string Id;
    std::string Name;
    std::string Type;
    std::map<std::string, std::string> ImageTags;
    bool IsFolder = false;
    long ProductionYear = 0;
    float CommunityRating = 0.0f;
    std::string Status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MediaItem, Id, Name, Type, ImageTags, IsFolder, ProductionYear, CommunityRating, Status);

struct MediaQueryResult {
    std::vector<MediaItem> Items;
    long TotalRecordCount = 0;
    long StartIndex = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaQueryResult, Items, TotalRecordCount, StartIndex);

}  // namespace jellyfin