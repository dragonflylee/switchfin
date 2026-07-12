/*
    GMCA — "Watchlist" sidebar tab (plex.tv account).
    Grid of 2:3 posters of watchlisted movies/shows, most recent first.
    API: GET discover.provider.plex.tv/library/sections/watchlist/all
    (api/plex/watchlist.hpp — ACCOUNT token), X-Plex-Container-* pagination.
    Items come from the PROVIDER (may not exist on the server): clicking
    matches by guid (plex::matchInLibrary) and opens the local detail page
    if found, otherwise notifies "not in library".

    Sort/filters (Y action, WatchlistFilter panel — watchlist_tab.cpp):
    provider sort (watchlistedAt/titleSort/originallyAvailableAt, asc/desc),
    provider Type filter (type=1|2), CLIENT-side Availability filter backed
    by the library guid cache (plex::fetchLibraryGuids) — the same cache is
    used to gray out posters absent from the server.
*/

#pragma once

#include <memory>
#include <unordered_set>

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;

class WatchlistTab : public AttachedView {
public:
    WatchlistTab();

    void onCreate() override;

    brls::View* getDefaultFocus() override;

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recycler, "watchlist/grid");

    /// Resets pagination and reloads the grid; `reloadGuids` also
    /// refreshes the library guid cache (initial load and "refresh"
    /// action — not a mere sort change).
    void refresh(bool reloadGuids);
    void doRequest();

    /// guids (plex://...) present on the active server — nullptr until
    /// fetchLibraryGuids has answered (or failed): unknown presence,
    /// no graying out and no effective Availability filter.
    std::shared_ptr<std::unordered_set<std::string>> libraryGuids;

    /// a DataSource has been set for the load in progress (pages whose
    /// items are all filtered out client-side do not set one)
    bool loaded = false;

    size_t startIndex = 0;
    size_t pageSize = 60;
};
