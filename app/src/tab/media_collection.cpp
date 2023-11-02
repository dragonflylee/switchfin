/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"

using namespace brls::literals;  // for _i18n

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType)
    : itemId(itemId), itemType(itemType), startIndex(0) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/collection.xml");
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    this->pageSize = this->recyclerSeries->spanCount * 3;

    if (itemType == jellyfin::mediaTypeMusicAlbum) {
        this->recyclerSeries->estimatedRowHeight = 240;
    }

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](brls::View* view) {
        this->startIndex = 0;
        return true;
    });
    this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](brls::View* view) {
        brls::View* filter = new MediaFilter(this);
        brls::Application::pushActivity(new brls::Activity(filter));
        brls::sync([filter]() { brls::Application::giveFocus(filter); });
        return true;
    });
    this->recyclerSeries->registerCell("Cell", VideoCardCell::create);
    this->recyclerSeries->onNextPage([this]() { this->doRequest(); });

    this->doRequest();
}

brls::View* MediaCollection::getDefaultFocus() { return this->recyclerSeries; }

void MediaCollection::doRequest() {
    static std::string sortBy[] = {
        "SortName",
        "DateCreated",
        "DatePlayed",
        "PremiereDate",
        "PlayCount",
        "CommunityRating",
        "Random",
    };

    HTTP::Form query = {
        {"parentId", this->itemId},
        {"sortBy", sortBy[selectedSort]},
        {"sortOrder", selectedOrder ? "Descending" : "Ascending"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"EnableImageTypes", "Primary"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startIndex)},
    };
    if (this->itemType.size() > 0) {
        query.insert(std::make_pair("IncludeItemTypes", this->itemType));
        query.insert(std::make_pair("Recursive", "true"));
    }

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recyclerSeries->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recyclerSeries->setDataSource(new VideoDataSource(r.Items));
                brls::Application::giveFocus(this->recyclerSeries);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recyclerSeries->getDataSource());
                dataSrc->appendData(r.Items);
                this->recyclerSeries->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerSeries->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, HTTP::encode_form(query));
}