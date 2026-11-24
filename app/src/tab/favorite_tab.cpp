/*
    Copyright 2025 dragonflylee
*/

#include "tab/favorite_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/jellyfin.hpp"
#include "utils/keybind.hpp"
#include <tuple>

using namespace brls::literals;  // for _i18n

FavoriteTab::FavoriteTab() {
    brls::Logger::debug("Tab FavoriteTab: create");
    this->inflateFromXMLRes("xml/tabs/favorite.xml");
}

FavoriteTab::~FavoriteTab() { brls::Logger::debug("View FavoriteTab: delete"); }

brls::View* FavoriteTab::create() { return new FavoriteTab(); }

void FavoriteTab::onCreate() {
    // 收藏页按媒体类型分组，顺序与网页版一致
    const std::tuple<const char*, std::string, float, float> groups[] = {
        {"Movie", "main/search/genres/movie"_i18n, 300, 175},
        {"Series", "main/search/genres/series"_i18n, 300, 175},
        {"Episode", "main/search/genres/episode"_i18n, 235, 325},
        {"BoxSet", "main/tabs/collection"_i18n, 300, 175},
        {"MusicAlbum", "main/search/genres/album"_i18n, 225, 175},
        {"PhotoAlbum", "main/search/genres/photo_album"_i18n, 225, 175},
    };

    for (auto& [type, title, frameHeight, itemWidth] : groups) {
        const char* itemType = type;
        auto* group = new RecylingVideo();
        group->setTitle(title);
        group->setFrameHeight(frameHeight);
        group->setItemWidth(itemWidth);
        group->setPageSize(6);
        group->onQuery([itemType](size_t start, size_t pageSize) {
            std::string query = HTTP::encode_form({
                {"userId", AppConfig::instance().getUserId()},
                {"filters", "IsFavorite"},
                {"includeItemTypes", itemType},
                {"enableImageTypes", "Primary,Backdrop,Thumb"},
                {"fields", "BasicSyncInfo,Chapters"},
                {"recursive", "true"},
                {"sortBy", "SortName"},
                {"sortOrder", "Ascending"},
                {"limit", std::to_string(pageSize)},
                {"startIndex", std::to_string(start)},
            });
            return fmt::format(fmt::runtime(jellyfin::apiUserLibrary), AppConfig::instance().getUserId(), query);
        });

        // 先保持可见，等 HRecyclerFrame 的骨架屏渲染；无数据时再隐藏
        this->boxFavorite->addView(group);
        this->groups.push_back(group);
    }

    auto actionRefresh = [this](brls::View* view) {
        for (auto group : this->groups) {
            group->doRequest(true);
        }
        return true;
    };
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}

void FavoriteTab::doRequest() {
    for (auto group : this->groups) {
        group->reset();
        group->doRequest();
    }
}
