/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"

using namespace brls::literals;  // for _i18n

class CollectionDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaItem>;

    explicit CollectionDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("CollectionDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_query({
                {"tag", item.ImageTags[jellyfin::imageTypePrimary]},
                {"maxWidth", "200"},
            }));

        cell->labelTitle->setText(item.Name);
        if (item.ProductionYear > 0) cell->labelYear->setText(std::to_string(item.ProductionYear));

        if (item.CommunityRating > 0)
            cell->labelDuration->setText(fmt::format("{:.1f}", item.CommunityRating));
        else
            cell->labelDuration->setText("");
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        auto& item = this->list.at(index);

        if (item.Type == jellyfin::mediaTypeSeries) {
            recycler->present(new MediaSeries(item));
        } else if (item.Type == jellyfin::mediaTypeFolder) {
            recycler->present(new MediaCollection(item.Id));
        } else if (item.Type == jellyfin::mediaTypeMovie) {
            VideoView* view = new VideoView(item);
            brls::sync([view]() { brls::Application::giveFocus(view); });
        }
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

MediaCollection::MediaCollection(const std::string& id) : itemId(id), startIndex(0) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/collection.xml");
    brls::Logger::debug("MediaCollection: create");
    this->pageSize = this->recyclerSeries->spanCount * 3;

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->startIndex = 0;
        return true;
    });
    this->recyclerSeries->registerCell("Cell", &VideoCardCell::create);
    this->recyclerSeries->onNextPage([this]() { this->doRequest(); });

    this->doRequest();
}

void MediaCollection::doRequest() {
    std::string query = HTTP::encode_query({
        {"ParentId", this->itemId},
        {"SortBy", "SortName"},
        {"Fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"Limit", std::to_string(this->pageSize)},
        {"StartIndex", std::to_string(this->startIndex)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaItem>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.StartIndex == 0) {
                this->recyclerSeries->setDataSource(new CollectionDataSource(r.Items));
                brls::Application::giveFocus(this->recyclerSeries);
            } else {
                auto dataSrc = dynamic_cast<CollectionDataSource*>(this->recyclerSeries->getDataSource());
                dataSrc->appendData(r.Items);
                this->recyclerSeries->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerSeries->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
}