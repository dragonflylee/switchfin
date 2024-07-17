/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include <fmt/format.h>

using namespace brls::literals;  // for _i18n

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType)
    : itemId(itemId), itemType(itemType), startIndex(0) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/collection.xml");
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    this->pageSize = this->recycler->spanCount * 3;

    if (itemType == jellyfin::mediaTypeMusicAlbum) {
        this->recycler->estimatedRowHeight = 240;
    }

    std::string serverUrl = AppConfig::instance().getUrl();
    this->prefKey = fmt::format("{}/web/index.html{}", serverUrl, itemType);
    std::transform(this->prefKey.begin(), this->prefKey.end(), this->prefKey.begin(),
        [](unsigned char c) { return std::tolower(c); });

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->startIndex = 0;
        this->doRequest();
        return true;
    });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (itemType == jellyfin::mediaTypePlaylist || AppConfig::SYNC) {
        this->doRequest();
    } else {
        this->doPreferences();
    }
}

brls::View* MediaCollection::getDefaultFocus() { return this->recycler; }

void MediaCollection::doPreferences() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::DisplayPreferences>(
        [ASYNC_TOKEN](const jellyfin::DisplayPreferences& r) {
            ASYNC_RELEASE
            this->prefId = std::move(r.Id);
            for (const auto& item : r.CustomPrefs.items()) {
                if (item.value().is_string()) {
                    this->customPrefs[item.key()] = item.value().get<std::string>();
                }
            }
            this->loadFilter();
            this->doRequest();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        },
        jellyfin::apiUserSetting, AppConfig::instance().getUser().id);

    this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        MediaFilter* filter = new MediaFilter();
        filter->getEvent()->subscribe([this]() {
            this->startIndex = 0;
            this->doRequest();
            if (AppConfig::SYNC) this->saveFilter();
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });
}

struct DisplaySort {
    std::string SortBy;
    std::string SortOrder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DisplaySort, SortBy, SortOrder);

void MediaCollection::loadFilter() {
    auto it = this->customPrefs.find(this->prefKey);
    if (it == this->customPrefs.end()) return;

    try {
        DisplaySort s = nlohmann::json::parse(it->second);
        MediaFilter::selectedOrder = s.SortOrder == "Ascending" ? 0 : 1;
        for (size_t i = 0; i < std::size(MediaFilter::sortList); i++) {
            if (MediaFilter::sortList[i] == s.SortBy) {
                MediaFilter::selectedSort = i;
            }
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("MediaCollection loadFilter: {}", ex.what());
    }
}

void MediaCollection::saveFilter() {
    nlohmann::json value = {
        {"SortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
        {"SortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
    };
    this->customPrefs[this->prefKey] = value.dump();

    jellyfin::postJSON(
        {
            {"Id", this->prefId},
            {"CustomPrefs", this->customPrefs},
            {"Client", "emby"},
        },
        [](...) {}, [](const std::string& ex) { brls::Logger::warning("usersettings upload: {}", ex); },
        jellyfin::apiUserSetting, AppConfig::instance().getUser().id);
}

void MediaCollection::doRequest() {
    std::vector<std::string> filters;
    if (MediaFilter::selectedPlayed) filters.push_back("IsPlayed");
    if (MediaFilter::selectedUnplayed) filters.push_back("IsUnplayed");

    HTTP::Form query = {
        {"parentId", this->itemId},
        {"sortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
        {"sortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
        {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
        {"EnableImageTypes", "Primary"},
        {"filters", fmt::format("{}", fmt::join(filters, ","))},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startIndex)},
    };
    if (this->itemType.size() > 0) {
        query.insert(std::make_pair("IncludeItemTypes", this->itemType));
        query.insert(std::make_pair("Recursive", "true"));
    }

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                brls::Application::giveFocus(this->recycler);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, HTTP::encode_form(query));
}