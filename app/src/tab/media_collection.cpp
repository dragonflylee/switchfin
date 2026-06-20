/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/h_recycling.hpp"
#include "view/presenter.hpp"
#include "tab/suggest_show.hpp"
#include "tab/suggest_movie.hpp"
#include "utils/genre_image.hpp"
#include "utils/image.hpp"
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
        // the genre Directory has no thumb (verified on a real server
        // 2026-06-10) -> Kometa poster via the server's photo transcoder
        // (genre_image.cpp); unknown genre -> placeholder set by
        // prepareForReuse (no request, the Kometa set is embedded)
        cell->labelTitle->setText(item.title);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
        // Kometa posters are keyed by the English genre name; the displayed
        // title may be localized (Stremio), so fall back to the key (raw value).
        std::string poster = GenreImage::posterUrl(item.title);
        if (poster.empty()) poster = GenreImage::posterUrl(item.key);
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

        ASYNC_RETAIN
        // genres -> Directory key/title
        AppConfig::instance().backend().getGenres(
            itemId, itemType == media::mediaTypeShow ? media::MediaKind::Show : media::MediaKind::Movie,
            [ASYNC_TOKEN, itemId, itemType](const media::Container<media::Section>& r) {
                ASYNC_RELEASE
                this->setDataSource(new GenresDataSource(r.Items, itemId, itemType));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            });
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
        ASYNC_RETAIN
        // requested offset, not r.StartIndex: see MediaCollection::doRequest —
        // Jellyfin/Emby omit StartIndex on an empty past-the-end page, so it
        // parses to 0 and would wipe a filled grid
        size_t reqStart = this->start;
        AppConfig::instance().backend().getCollections(this->sectionId, this->start, this->pageSize,
            [ASYNC_TOKEN, reqStart](const media::Container<media::Item>& r) {
                ASYNC_RELEASE
                this->start = reqStart + this->pageSize;
                if (r.TotalRecordCount == 0 && reqStart == 0) {
                    this->setEmpty();
                } else if (reqStart == 0) {
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
            });
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
        // and only when the backend actually has collections (Stremio has none).
        if (itemType == plex::mediaTypeMovie && AppConfig::instance().backend().caps().collections) {
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
    AppConfig::instance().backend().getItemDetail(
        this->itemId, false,
        [ASYNC_TOKEN](const media::Item& item) {
            ASYNC_RELEASE
            if (this->labelTitle) this->labelTitle->setText(item.title);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaCollection: metadata {}", ex);
        });
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
    media::GridQuery q;
    q.sortField = MediaFilter::sortList[MediaFilter::selectedSort];
    q.descending = MediaFilter::selectedOrder != 0;
    q.unwatchedOnly = MediaFilter::selectedUnplayed;
    q.genreId = this->genresId;
    if (this->itemType == media::mediaTypeMovie)
        q.kind = media::MediaKind::Movie;
    else if (this->itemType == media::mediaTypeShow)
        q.kind = media::MediaKind::Show;
    // photo / collection: no type= filter

    ASYNC_RETAIN
    // the offset we asked for: the response's StartIndex is unreliable on
    // Jellyfin/Emby — it is omitted from an empty past-the-end page (33-byte
    // body) and parses back to 0, which would otherwise mark a filled grid as
    // "empty". Plex echoes the real totalSize, so this was latent there.
    size_t reqStart = this->startIndex;
    auto onItems = [ASYNC_TOKEN, reqStart](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = reqStart + this->pageSize;
            // Only the FIRST page being empty means the library is empty. The
            // recycler pre-fetches the next page; that past-the-end page returns
            // TotalRecordCount=0 and must not wipe a grid filled by page 0.
            if (r.TotalRecordCount == 0 && reqStart == 0) {
                this->updateMeta(0, 0);
                this->recycler->setEmpty();
            } else if (reqStart == 0) {
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
    };
    auto onError = [ASYNC_TOKEN, reqStart](const std::string& ex) {
        ASYNC_RELEASE
        if (reqStart > 0) {
            brls::Application::notify(ex);
        } else {
            this->recycler->setError(ex);
        }
    };
    if (this->itemType == media::mediaTypeCollection)
        AppConfig::instance().backend().getCollectionChildren(
            this->itemId, this->startIndex, this->pageSize, onItems, onError);
    else
        AppConfig::instance().backend().getLibraryGrid(
            this->itemId, q, this->startIndex, this->pageSize, onItems, onError);
}

// ---- Stremio: catalogs-as-subtabs section view ---------------------------------

/// Paginated grid of ONE catalog (getLibraryGrid on a routed catalog key).
/// Mirror of CollectionsTab but backed by the library grid endpoint.
class CatalogGrid : public RecyclingGrid {
public:
    CatalogGrid(const std::string& catalogKey, const std::string& itemType)
        : catalogKey(catalogKey), itemType(itemType) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->spanCount = brls::getStyle().getMetric("app/grid/6");
        this->itemImageRatio = 1.5f;
        this->itemExtraHeight = 55;
        float side = brls::getStyle()["main/content_padding_sides"];
        this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);
        this->onNextPage([this] { this->doRequest(); });
        this->doRequest();
    }

    void doRequest() {
        ASYNC_RETAIN
        size_t reqStart = this->start;
        media::GridQuery q;
        if (this->itemType == media::mediaTypeMovie)
            q.kind = media::MediaKind::Movie;
        else if (this->itemType == media::mediaTypeShow)
            q.kind = media::MediaKind::Show;
        AppConfig::instance().backend().getLibraryGrid(this->catalogKey, q, this->start, this->pageSize,
            [ASYNC_TOKEN, reqStart](const media::Container<media::Item>& r) {
                ASYNC_RELEASE
                this->start = reqStart + this->pageSize;
                if (r.TotalRecordCount == 0 && reqStart == 0) {
                    this->setEmpty();
                } else if (reqStart == 0) {
                    this->setDataSource(new VideoDataSource(r.Items));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN, reqStart](const std::string& ex) {
                ASYNC_RELEASE
                if (reqStart == 0) this->setError(ex);
            });
    }

private:
    std::string catalogKey;
    std::string itemType;
    size_t start = 0;
    size_t pageSize = 60;
};

StremioCatalogs::StremioCatalogs(const std::string& sectionKey, const std::string& sectionType)
    : sectionKey(sectionKey), sectionType(sectionType) {
    brls::Logger::debug("StremioCatalogs: create {} type {}", sectionKey, sectionType);
    this->inflateFromXMLRes("xml/tabs/stremio_catalogs.xml");

    // one tab per catalog of this type (Populaires / Nouveautés / À la une / …)
    for (auto& t : AppConfig::instance().backend().sectionTabs(sectionKey)) {
        std::string catKey = t.first, type = sectionType;
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel(t.second);
        this->tabFrame->addTab(item, [catKey, type]() { return new CatalogGrid(catKey, type); });
    }
    // a Genres tab when the backend exposes genre directories
    if (AppConfig::instance().backend().caps().genres) {
        std::string key = sectionKey, type = sectionType;
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [key, type]() { return new GenresTab(key, type); });
    }
    this->tabFrame->registerTabAction(this);
}

brls::View* StremioCatalogs::getDefaultFocus() {
    if (brls::View* frame = this->getView("stremio/tabFrame")) return frame->getDefaultFocus();
    return AttachedView::getDefaultFocus();
}
