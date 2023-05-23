/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_folder.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class MediaFolderCell : public RecyclingGridItem {
public:
    MediaFolderCell() {
        view = new brls::Image();
        view->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->addView(view);
    }

    static MediaFolderCell* create() { return new MediaFolderCell(); }

    brls::Image* view;
};

class MediaFolderDataSource : public RecyclingGridDataSource {
public:
    void clearData() override {}

    MediaFolderDataSource(const jellyfin::MediaFolderResult& r) : list(std::move(r.Items)) {
        brls::Logger::debug("MediaFolderDataSource: create {}", r.Items.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        MediaFolderCell* cell = dynamic_cast<MediaFolderCell*>(recycler->dequeueReusableCell("Cell"));
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {}

private:
    std::vector<jellyfin::MediaFolder> list;
};

MediaFolders::MediaFolders() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/media_folder.xml");
    brls::Logger::debug("MediaFolders: create");
    this->recyclerFolders->registerCell("Cell", &MediaFolderCell::create);

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->onRequest();
        return true;
    });

    this->onRequest();
}

brls::View* MediaFolders::create() { return new MediaFolders(); }

void MediaFolders::onRequest() {
    ASYNC_RETAIN
    jellyfin::getJSON(fmt::format(jellyfin::apiUserViews, AppConfig::instance().getUserId()),
        [ASYNC_TOKEN](const jellyfin::MediaFolderResult& r) {
            ASYNC_RELEASE
            auto data = new MediaFolderDataSource(r);
            brls::Logger::debug("Get MediaFolder {}", r.Items.size());
            this->recyclerFolders->setDataSource(data);
        });
}