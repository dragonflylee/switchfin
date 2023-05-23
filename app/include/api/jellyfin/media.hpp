#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "/Users/{}/Views";

struct MediaFolder {
    std::string Id;
    std::string Name;
    std::string ServerId;
    std::string ParentId;
    std::string Type;
    bool IsFolder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaFolder, Id, Name, ServerId, ParentId, Type, IsFolder);

struct MediaFolderResult {
    std::vector<MediaFolder> Items;
    long TotalRecordCount;
    long StartIndex;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaFolderResult, Items, TotalRecordCount, StartIndex);

}