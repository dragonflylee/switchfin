/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"

using namespace brls::literals;  // for _i18n

class SeriesDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaItem>;

    explicit SeriesDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("SeriesDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        const std::string& url = AppConfig::instance().getServerUrl();
        std::string query = HTTP::encode_query({
            {"tag", item.ImageTags[jellyfin::imageTypePrimary]},
            {"fillHeight", std::to_string(240)},
        });
        Image::load(cell->picture, url + fmt::format(jellyfin::apiPrimaryImage, item.Id, query));

        cell->labelTitle->setText(item.Name);
        cell->labelYear->setText(std::to_string(item.ProductionYear));
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {}

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

MediaSeries::MediaSeries(const std::string& id) : itemId(id) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");
    brls::Logger::debug("MediaSeries: create");

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) { return true; });
    this->recyclerSeason->registerCell("Cell", &VideoCardCell::create);
    this->recyclerSeason->onNextPage([this]() { this->doRequest(); });

    this->doRequest();
}

void MediaSeries::doRequest() {
    std::string query = HTTP::encode_query({
        {"user_id", AppConfig::instance().getUserId()},
        {"Fields", "ItemCounts,PrimaryImageAspectRatio,BasicSyncInfo,CanDelete,MediaSourceCount"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(fmt::format(jellyfin::apiShowSeanon, this->itemId, query),
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult& r) {
            ASYNC_RELEASE

            this->recyclerSeason->setDataSource(new SeriesDataSource(r.Items));
            brls::Application::giveFocus(this->recyclerSeason);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerSeason->setError(ex);
        });
}