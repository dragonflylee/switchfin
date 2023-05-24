/*
    Copyright 2023 dragonflylee
*/

#include "tab/library_view.hpp"
#include "view/recycling_grid.hpp"
#include "api/jellyfin.hpp"
#include "utils/image.hpp"

using namespace brls::literals;  // for _i18n

class VideoCardCell : public RecyclingGridItem {
public:
    VideoCardCell() { this->inflateFromXMLRes("xml/view/video_card.xml"); }

    ~VideoCardCell() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    static VideoCardCell* create() { return new VideoCardCell(); }

    BRLS_BIND(brls::Image, picture, "video/card/picture");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelYear, "video/card/label/year");
};

class LibraryDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaSeries>;

    explicit LibraryDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("LibraryDataSource: create {}", r.size());
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

LibraryView::LibraryView(const std::string& id) : itemId(id) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/library_view.xml");
    brls::Logger::debug("LibraryView: create");
    this->pageSize = this->recyclerSeries->spanCount * 3;

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) { return true; });
    this->recyclerSeries->registerCell("Cell", &VideoCardCell::create);
    this->recyclerSeries->onNextPage([this]() { this->doRequest(); });

    this->doRequest();
}

void LibraryView::doRequest() {
    std::string query = HTTP::encode_query({
        {"ParentId", this->itemId},
        {"Limit", std::to_string(this->pageSize)},
        {"StartIndex", std::to_string(this->startIndex)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(fmt::format(jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query),
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaSeries>& r) {
            ASYNC_RELEASE
            this->startIndex += this->pageSize;
            if (r.StartIndex == 0) {
                this->recyclerSeries->setDataSource(new LibraryDataSource(r.Items));
                brls::Application::giveFocus(this->recyclerSeries);
            } else {
                auto dataSrc = dynamic_cast<LibraryDataSource*>(this->recyclerSeries->getDataSource());
                dataSrc->appendData(r.Items);
                this->recyclerSeries->notifyDataChanged();
            }
        });
}