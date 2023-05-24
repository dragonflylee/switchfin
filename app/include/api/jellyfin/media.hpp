#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "/Users/{}/Views";
const std::string apiImage = "/Items/{}/Images/Primary?";

struct MediaItem {
    std::string Id;
    std::string Name;
    std::string ServerId;
    std::string ParentId;
    std::string Type;
    std::string Etag;
    bool IsFolder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaItem, Id, Name, ServerId, ParentId, Type, Etag, IsFolder);

struct MediaQueryResult {
    std::vector<MediaItem> Items;
    long TotalRecordCount;
    long StartIndex;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaQueryResult, Items, TotalRecordCount, StartIndex);

}