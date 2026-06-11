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
#include "utils/genre_image.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

std::map<std::string, std::string> MediaCollection::customPrefs;

/// true when the current focus lives inside `view` — a tab loading in the
/// background must not steal the focus from the sidebar
static bool hasFocusWithin(brls::View* view) {
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
        if (v == view) return true;
    }
    return false;
}

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
        // the genre Directory has no thumb (verified on a real server
        // 2026-06-10) -> Kometa poster via the server's photo transcoder
        // (genre_image.cpp); unknown genre -> placeholder set by
        // prepareForReuse (no request, the Kometa set is embedded)
        cell->labelTitle->setText(item.title);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
        std::string poster = GenreImage::posterUrl(item.title);
        if (!poster.empty()) Image::with(cell->picture, poster);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        ui::presentDetail(recycler, new MediaCollection(this->itemId, this->itemType, item.key));
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
        this->spanCount = brls::getStyle().getMetric("app/grid/6");
        // 2:3 posters + label area, recomputed at layout (recycling_grid.cpp)
        this->itemImageRatio = 1.5f;
        this->itemExtraHeight = 55;
        // insets inside the scroll (collection.xml no longer has root
        // padding); top 70: the floating tab bar (60) passes in front
        float side = brls::getStyle()["main/content_padding_sides"];
        this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);

        int type = itemType == plex::mediaTypeShow ? plex::typeShow : plex::typeMovie;

        ASYNC_RETAIN
        // GET /library/sections/{key}/genre?type= -> Directory key/title
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
        this->itemImageRatio = 1.5f;
        this->itemExtraHeight = 55;
        // top 70: same contract as GenresTab (floating bar above)
        float side = brls::getStyle()["main/content_padding_sides"];
        this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);

        this->onNextPage([this] { this->doRequest(); });
        this->doRequest();
    }

    void doRequest() {
        HTTP::Form query;
        plex::addPagination(query, this->start, this->pageSize);

        ASYNC_RETAIN
        // GET /library/sections/{key}/collections
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
        // centered top tabs: home (from XML), then suggest, collections, genres

        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/suggest"_i18n);
        if (itemType == plex::mediaTypeShow) {
            this->tabFrame->addTab(item, [this]() { return new SuggestShow(this->itemId); });
        } else {
            this->tabFrame->addTab(item, [this]() { return new SuggestMovie(this->itemId); });
        }

        // Collections: movies only — useless on show libraries
        // (verified on a real server 2026-06-10:
        // /library/sections/{show}/collections -> size 0)
        if (itemType == plex::mediaTypeMovie) {
            item = new AutoSidebarItem();
            item->setTabStyle(AutoTabBarStyle::ACCENT);
            item->setFontSize(18);
            item->setLabel("main/media/collections"_i18n);
            this->tabFrame->addTab(item, [this]() { return new CollectionsTab(this->itemId); });
        }

        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [this]() { return new GenresTab(this->itemId, this->itemType); });

        this->tabFrame->registerTabAction(this);
    } else {
        this->inflateFromXMLRes("xml/tabs/media.xml");
        // collection mode (grid only): scrolled header "title + N items
        // · duration" like the playlist view;
        // set BEFORE the first layout (setHeaderView contract)
        if (itemType == plex::mediaTypeCollection) {
            brls::View* header = brls::View::createFromXMLResource("view/grid_header.xml");
            this->labelTitle = dynamic_cast<brls::Label*>(header->getView("grid/header/title"));
            this->labelMeta = dynamic_cast<brls::Label*>(header->getView("grid/header/meta"));
            this->recycler->setHeaderView(header, 84);
            this->doMetadata();
        }
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
        // sort preferences persisted LOCALLY (LIBRARY_SORT item):
        // /DisplayPreferences does not exist in Plex (PLEX_MIGRATION.md §2.5)
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

brls::View* MediaCollection::getDefaultFocus() {
    // library mode: delegate to the tab frame, which resolves the ACTIVE
    // tab and its focus memory. `recycler` is the Home tab's grid:
    // returning it directly while ANOTHER tab is shown gave focus to a
    // DETACHED tree (invisible cells, stale positions) — the "focus lost
    // when coming back from the sidebar" bug.
    // Resolution by id: no throw in collection mode (XML without tabFrame).
    if (brls::View* frame = this->getView("media/tabFrame")) return frame->getDefaultFocus();
    return this->recycler;
}

void MediaCollection::doMetadata() {
    ASYNC_RETAIN
    // header title: GET /library/metadata/{ratingKey} — a collection's
    // Metadata carries NEITHER duration NOR leafCount, only childCount
    // (verified on a real server 2026-06-10 on /library/metadata/1024133);
    // the displayed count thus comes from the grid's totalSize (doRequest)
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (this->labelTitle && !r.Items.empty()) this->labelTitle->setText(r.Items.front().title);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaCollection: metadata {}", ex);
        },
        plex::apiMetadata, this->itemId, "");
}

void MediaCollection::updateMeta(int64_t count, int64_t durationMs) {
    if (!this->labelMeta) return;  // never set outside collection mode
    if (count <= 0) {
        this->labelMeta->setVisibility(brls::Visibility::GONE);
        return;
    }
    std::string meta =
        fmt::format("{} {}", count, count > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n);
    if (durationMs > 0) {
        // same convention as the playlist view (playlist_view.cpp): h/min
        int min = int(durationMs / 60000);
        meta += min >= 60 ? fmt::format("  ·  {} h {:02d}", min / 60, min % 60) : fmt::format("  ·  {} min", min);
    }
    this->labelMeta->setText(meta);
    this->labelMeta->setVisibility(brls::Visibility::VISIBLE);
}

void MediaCollection::onCreate() {
    // sidebar tab embedding: the close cross only makes sense when presented
    brls::View* close = this->getView("media/close");
    if (close) close->setVisibility(brls::Visibility::GONE);
}

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

    // format "sortBy,sortOrder,filter" (e.g. "addedAt,1,0")
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
    // Plex sort: sort=field[:desc]
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
    // photo: no type= filter

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->updateMeta(0, 0);
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                // collection mode header meta: reliable count (totalSize);
                // total duration ONLY if this page covers the whole
                // collection — a collection's Metadata has no duration of
                // its own (verified on a real server 2026-06-10) and a
                // partial sum would be wrong
                if (this->labelMeta) {
                    int64_t total = 0;
                    if (r.TotalRecordCount <= (long)this->pageSize) {
                        for (auto& it : r.Items) {
                            // only movie/episode/clip carry a full duration
                            // (a show only exposes an episode duration)
                            bool full = it.type == plex::mediaTypeMovie || it.type == plex::mediaTypeEpisode ||
                                        it.type == plex::mediaTypeClip;
                            if (!full || it.duration <= 0) {
                                total = 0;
                                break;
                            }
                            total += it.duration;
                        }
                    }
                    this->updateMeta(r.TotalRecordCount, total);
                }
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                if (hasFocusWithin(this)) brls::Application::giveFocus(this->recycler);
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
