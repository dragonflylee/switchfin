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
    MediaFolderCell() {
        auto theme = brls::Application::getTheme();
        this->picture->setGrow(1.0f);
        this->labelTitle->setFontSize(35);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setBackgroundColor(theme.getColor("color/grey_2"));
        this->addView(picture);
        this->addView(labelTitle);
    }

    ~MediaFolderCell() override { Image::cancel(this->picture); }

    static RecyclingGridItem* create() { return new MediaFolderCell(); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    brls::Image* picture = new brls::Image();
    brls::Label* labelTitle = new brls::Label();
};

class MediaFolderDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaCollection>;

    MediaFolderDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("MediaFolderDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        MediaFolderCell* cell = dynamic_cast<MediaFolderCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id, HTTP::encode_form({{"tag", it->second}}));
            cell->labelTitle->setVisibility(brls::Visibility::GONE);
            cell->picture->setVisibility(brls::Visibility::VISIBLE);

        } else {
            cell->labelTitle->setText(item.Name);
            cell->labelTitle->setVisibility(brls::Visibility::VISIBLE);
            cell->picture->setVisibility(brls::Visibility::GONE);
        }
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        std::string itemType;
        if (item.CollectionType == "tvshows")
            itemType = jellyfin::mediaTypeSeries;
        else if (item.CollectionType == "movies")
            itemType = jellyfin::mediaTypeMovie;
        else if (item.CollectionType == "music")
            itemType = jellyfin::mediaTypeMusicAlbum;
        else if (item.CollectionType == "playlists")
            itemType = jellyfin::mediaTypePlaylist;

        recycler->present(new MediaCollection(item.Id, itemType));
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
    this->recyclerFolders->registerCell("Cell", MediaFolderCell::create);

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->doRequest();
        return true;
    });

    this->doRequest();
}

MediaFolders::~MediaFolders() { brls::Logger::debug("MediaFolders: deleted"); }

brls::View* MediaFolders::create() { return new MediaFolders(); }

void MediaFolders::doRequest() {
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaCollection>& r) {
            ASYNC_RELEASE
            if (r.Items.empty())
                this->recyclerFolders->setEmpty();
            else
                this->recyclerFolders->setDataSource(new MediaFolderDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerFolders->setError(ex);
        },
        jellyfin::apiUserViews, AppConfig::instance().getUser().id);
}