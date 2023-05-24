/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_folder.hpp"
#include "view/recycling_grid.hpp"
#include "api/jellyfin.hpp"
#include "utils/image.hpp"

using namespace brls::literals;  // for _i18n

class MediaFolderCell : public RecyclingGridItem {
public:
    MediaFolderCell() : view(new brls::Image()) {
        this->view->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->addView(view);
    }

    ~MediaFolderCell() { Image::cancel(this->view); }

    static MediaFolderCell* create() { return new MediaFolderCell(); }

    brls::Image* view;
};

class MediaFolderDataSource : public RecyclingGridDataSource {
public:
    void clearData() override { this->list.clear(); }

    MediaFolderDataSource(const jellyfin::MediaQueryResult& r) : list(std::move(r.Items)) {
        brls::Logger::debug("MediaFolderDataSource: create {}", r.Items.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        MediaFolderCell* cell = dynamic_cast<MediaFolderCell*>(recycler->dequeueReusableCell("Cell"));
        const std::string& url = AppConfig::instance().getServerUrl();
        Image::load(cell->view, url + fmt::format(jellyfin::apiImage, this->list[index].Id));
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {}

private:
    std::vector<jellyfin::MediaItem> list;
};

MediaFolders::MediaFolders() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/media_folder.xml");
    brls::Logger::debug("MediaFolders: create");
    this->recyclerFolders->registerCell("Cell", &MediaFolderCell::create);

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
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult& r) {
            ASYNC_RELEASE
            auto data = new MediaFolderDataSource(r);
            brls::Logger::debug("Get MediaFolder {}", r.Items.size());
            this->recyclerFolders->setDataSource(data);
        });
}