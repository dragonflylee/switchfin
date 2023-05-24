/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_folder.hpp"
#include "tab/library_view.hpp"
#include "view/recycling_grid.hpp"
#include "api/jellyfin.hpp"
#include "utils/image.hpp"

using namespace brls::literals;  // for _i18n

class MediaFolderCell : public RecyclingGridItem {
public:
    MediaFolderCell() : picture(new brls::Image()) {
        this->picture->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->addView(picture);
    }

    ~MediaFolderCell() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    brls::Image* picture;
};

class MediaFolderDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaItem>;

    MediaFolderDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("MediaFolderDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        MediaFolderCell* cell = dynamic_cast<MediaFolderCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        const std::string& url = AppConfig::instance().getServerUrl();
        std::string query = HTTP::encode_query({{"tag", item.ImageTags[jellyfin::imageTypePrimary]}});
        Image::load(cell->picture, url + fmt::format(jellyfin::apiPrimaryImage, item.Id, query));
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        recycler->present(new LibraryView(this->list[index].Id));
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

MediaFolders::MediaFolders() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/media_folder.xml");
    brls::Logger::debug("MediaFolders: create");
    this->recyclerFolders->registerCell("Cell", []() { return new MediaFolderCell(); });

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->doRequest();
        return true;
    });

    this->doRequest();
}

brls::View* MediaFolders::create() { return new MediaFolders(); }

void MediaFolders::doRequest() {
    ASYNC_RETAIN
    jellyfin::getJSON(fmt::format(jellyfin::apiUserViews, AppConfig::instance().getUserId()),
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<>& r) {
            ASYNC_RELEASE
            if (r.StartIndex == 0) {
                this->recyclerFolders->setDataSource(new MediaFolderDataSource(r.Items));
                brls::Application::giveFocus(this->recyclerFolders);
            } else {
                auto dataSrc = dynamic_cast<MediaFolderDataSource*>(this->recyclerFolders->getDataSource());
                dataSrc->appendData(r.Items);
                this->recyclerFolders->notifyDataChanged();
            }
        });
}