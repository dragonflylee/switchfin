/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"

using namespace brls::literals;  // for _i18n

class SeriesDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaItem>;

    explicit SeriesDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end())
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "200"}}));

        cell->labelTitle->setText(item.Name);
        cell->labelExt->setText(item.ProductionYear > 0 ? std::to_string(item.ProductionYear) : "");
        cell->labelRating->setText(item.CommunityRating > 0 ? fmt::format("{:.1f}", item.CommunityRating) : "");
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        auto& item = this->list.at(index);

        if (item.Type == jellyfin::mediaTypeSeries) {
            brls::View* view = dynamic_cast<brls::View*>(recycler);
            view->present(new MediaSeries(item));
        } else if (item.Type == jellyfin::mediaTypeFolder) {
            brls::View* view = dynamic_cast<brls::View*>(recycler);
            view->present(new MediaCollection(item.Id));
        } else if (item.Type == jellyfin::mediaTypeMovie || item.Type == jellyfin::mediaTypeEpisode) {
            VideoView* view = new VideoView(item);
            view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
            brls::sync([view]() { brls::Application::giveFocus(view); });
        } else {
            brls::Logger::debug("unsupport type {}", item.Type);
        }
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType)
    : itemId(itemId), itemType(itemType), startIndex(0) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/collection.xml");
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    this->pageSize = this->recyclerSeries->spanCount * 3;

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->startIndex = 0;
        return true;
    });
    this->recyclerSeries->registerCell("Cell", &VideoCardCell::create);
    this->recyclerSeries->onNextPage([this]() { this->doRequest(); });

    this->doRequest();
}

brls::View* MediaCollection::getDefaultFocus() { return this->recyclerSeries; }

void MediaCollection::doRequest() {
    std::string query = HTTP::encode_form({
        {"parentId", this->itemId},
        {"sortBy", "PremiereDate"},
        {"sortOrder", "Descending"},
        {"IncludeItemTypes", this->itemType},
        {"Recursive", "true"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"EnableImageTypes", "Primary"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startIndex)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaItem>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.StartIndex == 0) {
                this->recyclerSeries->setDataSource(new SeriesDataSource(r.Items));
                brls::Application::giveFocus(this->recyclerSeries);
            } else {
                auto dataSrc = dynamic_cast<SeriesDataSource*>(this->recyclerSeries->getDataSource());
                dataSrc->appendData(r.Items);
                this->recyclerSeries->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerSeries->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}