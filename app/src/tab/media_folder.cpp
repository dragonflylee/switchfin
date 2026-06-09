/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_folder.hpp"
#include "tab/media_collection.hpp"
#include "view/recycling_grid.hpp"
#include "view/auto_tab_frame.hpp"
#include "api/plex.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

class MediaFolderCell : public RecyclingGridItem {
public:
    MediaFolderCell() {
        auto theme = brls::Application::getTheme();
        this->picture->setGrow(1.0f);
        this->picture->setScalingType(brls::ImageScalingType::FILL);
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
    using MediaList = std::vector<plex::Section>;

    MediaFolderDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("MediaFolderDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        MediaFolderCell* cell = dynamic_cast<MediaFolderCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        // pas d'image fiable pour une section Plex (composite serveur) → titre texte
        cell->labelTitle->setText(item.title);
        cell->labelTitle->setVisibility(brls::Visibility::VISIBLE);
        cell->picture->setVisibility(brls::Visibility::GONE);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        recycler->present(new MediaCollection(item.key, item.type));
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
    this->recycler->registerCell("Cell", MediaFolderCell::create);
}

MediaFolders::~MediaFolders() { brls::Logger::debug("MediaFolders: deleted"); }

brls::View* MediaFolders::create() { return new MediaFolders(); }

void MediaFolders::onCreate() {
    auto doRefresh = [this](...) {
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    };
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, doRefresh);
    this->registerAction(KeyBind::getRefresh(), doRefresh);

    this->doRequest();
}

void MediaFolders::doRequest() {
    ASYNC_RETAIN
    // GET /library/sections → Directory[] (plex_client.dart:901-906)
    plex::getJSON<plex::Container<plex::Section>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Section>& r) {
            ASYNC_RELEASE
            // Hors périmètre v1 : musique et autres types (cf. PLEX_MIGRATION.md D2/D4)
            MediaFolderDataSource::MediaList items;
            for (auto& item : r.Items) {
                if (item.hidden) continue;
                if (item.type != plex::mediaTypeMovie && item.type != plex::mediaTypeShow &&
                    item.type != plex::mediaTypePhoto)
                    continue;
                items.push_back(item);
            }
            if (items.empty())
                this->recycler->setEmpty();
            else
                this->recycler->setDataSource(new MediaFolderDataSource(items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);

            auto dialog = new brls::Dialog(ex);
            dialog->addButton("hints/retry"_i18n, [this]() {
                brls::sync([this]() {
                    this->recycler->showSkeleton();
                    this->doRequest();
                });
            });
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
        },
        plex::apiSections);
}
