/*
    GMCA — "Watchlist" sidebar tab (see watchlist_tab.hpp).
*/

#include "tab/watchlist_tab.hpp"
#include "tab/media_movie.hpp"
#include "tab/media_series.hpp"
#include "api/plex/watchlist.hpp"
#include "api/backend.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/auto_tab_frame.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

/// Provider poster (absolute URL: tmdb, metadata-static.plex.tv —
/// ORIGINAL sizes, several MB) proxied through the server's photo
/// transcoder to get a thumbnail: /photo/:/transcode?url=<absolute>&width&height.
static void loadProviderImage(brls::Image* view, const std::string& url, int width, int height) {
    if (url.empty()) return;
    Image::with(view, AppConfig::instance().backend().imageUrlExternal(url, width, height));
}

/// Sort/filters side panel (Y action) — same pattern as MediaFilter
/// (media_filter.cpp), but watchlist-specific options: only the sorts
/// HONORED by discover.provider are exposed (verified with real GETs, see
/// plex::fetchWatchlist) and the Availability filter is purely client-side.
class WatchlistFilter : public brls::Box {
public:
    WatchlistFilter() {
        this->inflateFromXMLRes("xml/view/watchlist_filter.xml");
        brls::Logger::debug("WatchlistFilter: create");

        this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
            return true;
        });

        this->cancel->registerClickAction([this](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
            return true;
        });
        this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

        this->sortBy->init("main/media/sort_by"_i18n,
            {
                "main/media/date_add"_i18n,
                "main/media/name"_i18n,
                "main/media/premiere_date"_i18n,
            },
            selectedSort, [](int selected) { selectedSort = selected; });

        this->sortOrder->init("main/media/order"_i18n,
            {
                "main/media/ascending"_i18n,
                "main/media/descending"_i18n,
            },
            selectedOrder, [](int selected) { selectedOrder = selected; });

        this->filterType->init("main/remote/type"_i18n,
            {
                "main/watchlist/all"_i18n,
                "main/person/movies"_i18n,
                "main/person/shows"_i18n,
            },
            selectedType, [](int selected) { selectedType = selected; });

        this->filterAvailability->init("main/watchlist/availability"_i18n,
            {
                "main/watchlist/all"_i18n,
                "main/watchlist/on_server"_i18n,
                "main/watchlist/not_on_server"_i18n,
            },
            selectedAvailability, [](int selected) { selectedAvailability = selected; });
    }

    ~WatchlistFilter() override { brls::Logger::debug("WatchlistFilter: delete"); }

    bool isTranslucent() override { return true; }

    brls::VoidEvent* getEvent() { return &this->event; }

    /// (Session) state shared with WatchlistTab::doRequest
    inline static int selectedSort = 0;   // index into sortList
    inline static int selectedOrder = 1;  // 0 ascending, 1 descending
    inline static int selectedType = 0;   // 0 all, 1 movies, 2 shows
    inline static int selectedAvailability = 0;  // 0 all, 1 on server, 2 absent

    /// Honored provider sort fields, aligned with the selector labels
    inline static std::string sortList[] = {
        "watchlistedAt",
        "titleSort",
        "originallyAvailableAt",
    };

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "watchlist/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "watchlist/sort/order");
    BRLS_BIND(brls::SelectorCell, filterType, "watchlist/filter/type");
    BRLS_BIND(brls::SelectorCell, filterAvailability, "watchlist/filter/availability");

    brls::VoidEvent event;
};

/// 2:3 poster cards: provider poster + title + year.
/// Not a VideoDataSource: the primary click does the provider->server
/// matching before opening the detail page, and the X/long-press context
/// menu of VideoCardCell (which casts to VideoDataSource) stays inert
/// here — provider ratingKeys do not exist on the server.
class WatchlistDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;
    using GuidSet = std::shared_ptr<std::unordered_set<std::string>>;

    WatchlistDataSource(const MediaList& r, GuidSet guids) : list(std::move(r)), guids(std::move(guids)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);
        cell->labelTitle->setText(item.title);
        if (item.year > 0) {
            cell->labelExt->setText(std::to_string(item.year));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        }
        // recycled cell: purge the previous media's poster
        cell->picture->clear();
        loadProviderImage(cell->picture, item.thumb, 325, 488);
        // neither "watched" badge nor progress: server states, not provider's
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        // title absent from the server (guid cache): poster and texts
        // dimmed — alpha ALWAYS set (1.0 when rebinding a recycled cell
        // that showed an absent one). No set (nullptr) = unknown
        // presence: no dimming.
        bool present = !this->guids || this->guids->count(item.guid) > 0;
        cell->picture->setAlpha(present ? 1.0f : 0.4f);
        cell->labelTitle->setAlpha(present ? 1.0f : 0.5f);
        cell->labelExt->setAlpha(present ? 1.0f : 0.5f);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        // provider -> server matching by guid (plex::matchInLibrary);
        // the MediaMovie/MediaSeries pages require a SERVER ratingKey
        AppConfig::instance().backend().matchInLibrary(
            item.guid,
            [recycler, item](const media::Item& found) {
                if (found.ratingKey.empty()) {
                    brls::Application::notify("main/watchlist/not_in_library"_i18n);
                    return;
                }
                if (found.type == media::mediaTypeShow) {
                    ui::presentDetail(recycler, new MediaSeries(found));
                } else {
                    ui::presentDetail(recycler, new MediaMovie(found));
                }
            },
            [](const std::string& ex) { brls::Application::notify(ex); });
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
    GuidSet guids;
};

WatchlistTab::WatchlistTab() {
    brls::Logger::debug("WatchlistTab: create");
    this->inflateFromXMLRes("xml/tabs/watchlist.xml");

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });
}

void WatchlistTab::onCreate() {
    // capability gate: backends without a watchlist (Jellyfin/Emby) show a
    // graceful empty state instead of running the Plex provider calls.
    // (Fully hiding the tab from the bar is a follow-up — the icon-only tabs
    // have empty labels, so AutoTabFrame::clearTab cannot target them; the
    // clean fix is to add Watchlist/Playlists dynamically in MainTabFrame
    // gated by caps, like the library tabs.)
    if (AppConfig::instance().backend().caps().listKind == media::ListKind::None) {
        this->recycler->setEmpty();
        return;
    }

    auto actionRefresh = [this](...) {
        this->refresh(true);
        return true;
    };
    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    // sort/filters panel (same Y "Sorted" hint as the libraries);
    // on return, reload ONLY if a setting changed — the guid cache is not
    // affected by a sort/filter change
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        auto before = std::make_tuple(WatchlistFilter::selectedSort, WatchlistFilter::selectedOrder,
            WatchlistFilter::selectedType, WatchlistFilter::selectedAvailability);
        WatchlistFilter* filter = new WatchlistFilter();
        filter->getEvent()->subscribe([this, before]() {
            auto after = std::make_tuple(WatchlistFilter::selectedSort, WatchlistFilter::selectedOrder,
                WatchlistFilter::selectedType, WatchlistFilter::selectedAvailability);
            if (after != before) this->refresh(false);
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });

    this->refresh(true);
}

brls::View* WatchlistTab::getDefaultFocus() { return this->recycler; }

brls::View* WatchlistTab::create() { return new WatchlistTab(); }

void WatchlistTab::refresh(bool reloadGuids) {
    this->startIndex = 0;
    this->loaded = false;
    this->recycler->showSkeleton();
    // the guid cache (Plex availability dimming) only applies to the plex.tv
    // watchlist; Jellyfin/Emby favorites are server items, no guid round-trip
    bool plexWatchlist = AppConfig::instance().backend().caps().listKind == media::ListKind::Watchlist;
    // guid cache to (re)load: initial load/refresh, or Availability filter
    // active while a previous load failed
    if (plexWatchlist && (reloadGuids || (!this->libraryGuids && WatchlistFilter::selectedAvailability != 0))) {
        ASYNC_RETAIN
        plex::fetchLibraryGuids(
            [ASYNC_TOKEN](std::shared_ptr<std::unordered_set<std::string>> guids) {
                ASYNC_RELEASE
                this->libraryGuids = guids;
                this->doRequest();
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                // degradation: no dimming and no Availability filter,
                // but the watchlist stays browsable
                brls::Logger::warning("WatchlistTab: fetchLibraryGuids {}", ex);
                this->libraryGuids = nullptr;
                this->doRequest();
            });
    } else {
        this->doRequest();
    }
}

void WatchlistTab::doRequest() {
    // provider sort: honored field + :asc|:desc suffix (verified, see
    // plex::fetchWatchlist); provider Type filter via type=1|2
    std::string sort = WatchlistFilter::sortList[WatchlistFilter::selectedSort];
    sort += WatchlistFilter::selectedOrder ? ":desc" : ":asc";
    media::MediaKind kind = WatchlistFilter::selectedType == 1   ? media::MediaKind::Movie
                            : WatchlistFilter::selectedType == 2 ? media::MediaKind::Show
                                                                 : media::MediaKind::Any;
    bool favorites = AppConfig::instance().backend().caps().listKind == media::ListKind::Favorites;

    ASYNC_RETAIN
    // personal list: Plex watchlist (provider items) or Jellyfin favorites (server items)
    AppConfig::instance().backend().listWatchlist(
        sort, kind, this->startIndex, this->pageSize,
        [ASYNC_TOKEN, favorites](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            bool more = !r.Items.empty() && (long)this->startIndex < r.TotalRecordCount;

            if (favorites) {
                // favorites are normal server items -> standard grid (click opens
                // the detail page, context menu works); no provider images, no dimming
                if (!this->loaded) {
                    if (!r.Items.empty()) {
                        this->loaded = true;
                        this->recycler->setDataSource(new VideoDataSource(r.Items));
                    } else if (more) {
                        this->doRequest();
                    } else {
                        this->recycler->setEmpty("main/favorites/empty_title"_i18n,
                            "main/favorites/empty_sub"_i18n, "icon/ico-bookmark.svg");
                    }
                } else if (!r.Items.empty()) {
                    auto* ds = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                    if (ds) {
                        ds->appendData(r.Items);
                        this->recycler->notifyDataChanged();
                    }
                } else if (more) {
                    this->doRequest();
                }
                return;
            }

            // Availability filter: CLIENT-side (the provider knows nothing
            // about the server), backed by the guid cache; without a cache
            // (load failure), everything passes — unknown presence
            std::vector<plex::Item> items;
            int avail = WatchlistFilter::selectedAvailability;
            if (avail == 0 || !this->libraryGuids) {
                items = r.Items;
            } else {
                for (auto& item : r.Items) {
                    bool present = this->libraryGuids->count(item.guid) > 0;
                    if (present == (avail == 1)) items.push_back(item);
                }
            }

            if (!this->loaded) {
                if (!items.empty()) {
                    this->loaded = true;
                    this->recycler->setDataSource(new WatchlistDataSource(items, this->libraryGuids));
                } else if (more) {
                    // fully filtered page: chain while there are more
                    this->doRequest();
                } else if (r.TotalRecordCount == 0 && avail == 0 && WatchlistFilter::selectedType == 0) {
                    this->recycler->setEmpty(
                        "main/watchlist/empty_title"_i18n, "main/watchlist/empty_sub"_i18n, "icon/ico-bookmark.svg");
                } else {
                    // empty because of the filters: generic empty state
                    this->recycler->setEmpty();
                }
            } else if (!items.empty()) {
                auto dataSrc = dynamic_cast<WatchlistDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(items);
                this->recycler->notifyDataChanged();
            } else if (more) {
                this->doRequest();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            if (this->loaded) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        });
}
