/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/plex.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/h_recycling.hpp"
#include "tab/suggest_show.hpp"
#include "tab/suggest_movie.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

std::map<std::string, std::string> MediaCollection::customPrefs;

class GenresDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Section>;

    explicit GenresDataSource(const MediaList& r, const std::string& itemId, const std::string& itemType)
        : list(std::move(r)), itemId(itemId), itemType(itemType) {
        brls::Logger::debug("GenresDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        // le Directory de genre n'a pas de thumb → cellule texte
        cell->labelTitle->setText(item.title);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        recycler->present(new MediaCollection(this->itemId, this->itemType, item.key));
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
    std::string itemId;
    std::string itemType;
};

class GenresTab : public RecyclingGrid {
public:
    GenresTab(const std::string& itemId, const std::string& itemType) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->estimatedRowHeight = brls::getStyle().getMetric("app/album/height");
        this->spanCount = brls::getStyle().getMetric("app/grid/6");

        int type = itemType == plex::mediaTypeShow ? plex::typeShow : plex::typeMovie;

        ASYNC_RETAIN
        // GET /library/sections/{key}/genre?type= → Directory key/title
        plex::getJSON<plex::Container<plex::Section>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN, itemId, itemType](const plex::Container<plex::Section>& r) {
                ASYNC_RELEASE
                this->setDataSource(new GenresDataSource(r.Items, itemId, itemType));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            plex::apiSectionGenres, itemId, type);
    }
};

class CollectionsTab : public RecyclingGrid {
public:
    CollectionsTab(const std::string& sectionId) : sectionId(sectionId) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->spanCount = brls::getStyle().getMetric("app/grid/6");

        this->onNextPage([this] { this->doRequest(); });
        this->doRequest();
    }

    void doRequest() {
        HTTP::Form query;
        plex::addPagination(query, this->start, this->pageSize);

        ASYNC_RETAIN
        // GET /library/sections/{key}/collections (plex_client.dart:2444-2462)
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->setEmpty();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new VideoDataSource(r.Items));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            plex::apiCollections, this->sectionId, HTTP::encode_form(query));
    }

private:
    std::string sectionId;
    size_t start = 0;
    size_t pageSize = 60;
};

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType, const std::string& genresId)
    : itemId(itemId), genresId(genresId), itemType(itemType), startIndex(0) {
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    if (genresId.size() > 0) {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    } else if (itemType == plex::mediaTypeMovie || itemType == plex::mediaTypeShow) {
        this->inflateFromXMLRes("xml/tabs/collection.xml");
        // add genres tab
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [this]() { return new GenresTab(this->itemId, this->itemType); });

        // add collections tab
        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/media/collections"_i18n);
        this->tabFrame->addTab(item, [this]() { return new CollectionsTab(this->itemId); });

        this->tabFrame->registerTabAction(this);

        // add suggest tab
        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        if (itemType == plex::mediaTypeShow) {
            item->setLabel("main/tabs/suggest"_i18n);
            this->tabFrame->addTab(item, [this]() { return new SuggestShow(this->itemId); });
        } else if (itemType == plex::mediaTypeMovie) {
            item->setLabel("main/tabs/suggest"_i18n);
            this->tabFrame->addTab(item, [this]() { return new SuggestMovie(this->itemId); });
        }
    } else {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    }

    this->pageSize = this->recycler->spanCount * 3;

    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (AppConfig::SYNC) {
        // préférences de tri persistées LOCALEMENT (item LIBRARY_SORT) :
        // /DisplayPreferences n'existe pas chez Plex (PLEX_MIGRATION.md §2.5)
        if (MediaCollection::customPrefs.empty()) {
            auto saved = AppConfig::instance().getItem(AppConfig::LIBRARY_SORT, nlohmann::json::object());
            for (auto& el : saved.items()) {
                if (el.value().is_string()) {
                    MediaCollection::customPrefs[el.key()] = el.value().get<std::string>();
                }
            }
        }
        this->loadFilter();
        this->doRequest();
    } else {
        this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
            MediaFilter* filter = new MediaFilter();
            filter->getEvent()->subscribe([this]() {
                this->startIndex = 0;
                this->recycler->showSkeleton();
                this->doRequest();
            });
            brls::Application::pushActivity(new brls::Activity(filter));
            return true;
        });

        this->doRequest();
    }
}

brls::View* MediaCollection::getDefaultFocus() { return this->recycler; }

void MediaCollection::loadFilter() {
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        MediaFilter* filter = new MediaFilter();
        filter->getEvent()->subscribe([this]() {
            this->startIndex = 0;
            this->recycler->showSkeleton();
            this->doRequest();
            this->saveFilter();
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });

    auto it = MediaCollection::customPrefs.find(this->itemId);
    if (it == MediaCollection::customPrefs.end()) return;

    // format "sortBy,sortOrder,filtre" (ex. "addedAt,1,0")
    std::stringstream ss(it->second);
    std::string sortBy, sortOrder, unwatched;
    std::getline(ss, sortBy, ',');
    std::getline(ss, sortOrder, ',');
    std::getline(ss, unwatched, ',');

    for (size_t i = 0; i < std::size(MediaFilter::sortList); i++) {
        if (MediaFilter::sortList[i] == sortBy) {
            MediaFilter::selectedSort = i;
        }
    }
    MediaFilter::selectedOrder = sortOrder == "1" ? 1 : 0;
    MediaFilter::selectedUnplayed = unwatched == "1";
}

void MediaCollection::saveFilter() {
    MediaCollection::customPrefs[this->itemId] =
        fmt::format("{},{},{}", MediaFilter::sortList[MediaFilter::selectedSort], MediaFilter::selectedOrder,
            MediaFilter::selectedUnplayed ? 1 : 0);

    nlohmann::json value(MediaCollection::customPrefs);
    AppConfig::instance().setItem(AppConfig::LIBRARY_SORT, value);
}

void MediaCollection::doRequest() {
    HTTP::Form query;
    // tri Plex : sort=champ[:desc] (library_query_translator.dart:51-105)
    std::string sort = MediaFilter::sortList[MediaFilter::selectedSort];
    if (MediaFilter::selectedOrder) sort += ":desc";
    query["sort"] = sort;
    if (MediaFilter::selectedUnplayed) query["unwatched"] = "1";
    if (this->genresId.size() > 0) query["genre"] = this->genresId;
    plex::addPagination(query, this->startIndex, this->pageSize);

    std::string_view path = plex::apiSectionAll;
    if (this->itemType == plex::mediaTypeCollection) {
        path = plex::apiCollectionChildren;
    } else if (this->itemType == plex::mediaTypeMovie) {
        query["type"] = std::to_string(plex::typeMovie);
    } else if (this->itemType == plex::mediaTypeShow) {
        query["type"] = std::to_string(plex::typeShow);
    }
    // photo : pas de filtre type=

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                brls::Application::giveFocus(this->recycler);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            if (this->startIndex > 0) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        },
        path, this->itemId, HTTP::encode_form(query));
}
