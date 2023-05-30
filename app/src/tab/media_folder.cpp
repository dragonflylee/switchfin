/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_folder.hpp"
#include "tab/media_collection.hpp"
#include "view/recycling_grid.hpp"
#include "view/auto_tab_frame.hpp"
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

        std::string query = HTTP::encode_query({{"tag", item.ImageTags[jellyfin::imageTypePrimary]}});
        Image::load(cell->picture, jellyfin::apiPrimaryImage, AppConfig::instance().getServerUrl(), item.Id, query);
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        recycler->present(new MediaCollection(this->list[index].Id));
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

MediaFolders::~MediaFolders() { brls::Logger::debug("MediaFolders: deleted"); }

brls::View* MediaFolders::create() { return new MediaFolders(); }

AutoTabFrame* MediaFolders::getTabFrame() {
    brls::View* view = this->getParent();
    while (view) {
        AutoTabFrame* tabframe = dynamic_cast<AutoTabFrame*>(view);
        if (tabframe) return tabframe;
        view = view->getParent();
    }
    return nullptr;
}

void MediaFolders::doRequest() {
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaItem>& r) {
            ASYNC_RELEASE

            this->recyclerFolders->setDataSource(new MediaFolderDataSource(r.Items));

            if (this->getTabFrame()->getActiveTab() == this) brls::Application::giveFocus(this->recyclerFolders);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerFolders->setError(ex);
        },
        jellyfin::apiUserViews, AppConfig::instance().getUserId());
}