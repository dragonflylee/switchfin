/*
    GMCA — plex.tv account watchlist (discover/metadata.provider.plex.tv).

    ACCOUNT-level feature: all provider requests use the plex.tv token of the
    active profile (AppConfig::getAccountToken()), NOT the server token.
    Items returned by the provider are "universal" metadata (provider
    ratingKey, guid plex://movie|show/..., images as absolute URLs) that may
    not exist on the user's server: matching is done by guid (matchInLibrary).

    Endpoints come from the official provider API (Plex Web / python-plexapi),
    verified with real GETs on 2026-06-10 — detailed findings in
    api/plex/types.hpp (plex.tv provider section).
*/

#pragma once

#include <memory>
#include <unordered_set>

#include "api/plex.hpp"

namespace plex {

/// PROVIDER ratingKey of a Plex guid: "plex://movie/5d77..." -> "5d77...".
/// Empty string if the guid does not come from the Plex agent (legacy TMDB
/// agent...): in that case the title is not addressable on the provider.
inline std::string providerRatingKey(const std::string& guid) {
    if (guid.rfind("plex://", 0) != 0) return "";
    auto pos = guid.rfind('/');
    if (pos == std::string::npos || pos + 1 >= guid.size()) return "";
    return guid.substr(pos + 1);
}

/// The Plex watchlist only accepts movies and shows (no episodes/seasons:
/// plex://episode/... guids exist but addToWatchlist rejects them on Plex Web
/// too — the action is only offered on movie/show there).
inline bool watchlistable(const Item& item) {
    return (item.type == mediaTypeMovie || item.type == mediaTypeShow) && !providerRatingKey(item.guid).empty();
}

/// Default watchlist sort: most recently added first.
const std::string watchlistDefaultSort = "watchlistedAt:desc";

/// Paginated watchlist. `then` receives the provider Container:
/// Items[].ratingKey is the PROVIDER ratingKey, thumb an absolute URL.
///
/// Parameters VERIFIED with real GETs on discover.provider (2026-06-10):
///  - sort: watchlistedAt, titleSort, originallyAvailableAt (:asc and :desc
///    suffixes honored for all three). `title` and `year` are silently
///    IGNORED (default order returned) — do not expose them. `rating:desc`
///    responds but the observed order is unusable (clusters of ties).
///  - type: 1 (movies) | 2 (shows) filters correctly (714/167 out of 884
///    observed); `libtype=movie|show` (python-plexapi) is IGNORED here.
///  - X-Plex-Container-Size: capped at 100 (400 "Invalid value" beyond).
/// Empty `sort` -> watchlistDefaultSort; `type` not in {typeMovie, typeShow} -> all.
inline void fetchWatchlist(size_t start, size_t size, const std::string& sort, int type,
    const std::function<void(Container<Item>)>& then, OnError error) {
    HTTP::Form query = {{"sort", sort.empty() ? watchlistDefaultSort : sort}};
    if (type == typeMovie || type == typeShow) query["type"] = std::to_string(type);
    addPagination(query, start, size);
    getJSON<Container<Item>>(discoverApiBase, AppConfig::instance().getAccountToken(), then, error, apiWatchlistAll,
        HTTP::encode_form(query));
}

/// "Is watchlisted" state of a title identified by its plex:// guid.
/// The server metadata exposes nothing (verified): we query the provider —
/// `watchlistedAt` present iff watchlisted. `then(false)` if non-provider guid.
inline void fetchWatchlistState(const std::string& guid, const std::function<void(bool)>& then, OnError error) {
    std::string key = providerRatingKey(guid);
    if (key.empty()) {
        if (then) then(false);
        return;
    }
    getJSON<Container<Item>>(
        metadataApiBase, AppConfig::instance().getAccountToken(),
        [then](const Container<Item>& r) {
            if (then) then(!r.Items.empty() && r.Items.front().watchlistedAt > 0);
        },
        error, apiProviderMetadata, key);
}

/// Adds (add=true) or removes (add=false) a title from the account watchlist.
/// PUT /actions/addToWatchlist|removeFromWatchlist?ratingKey={provider}.
inline void setWatchlisted(const std::string& guid, bool add, const std::function<void()>& then, OnError error) {
    std::string key = providerRatingKey(guid);
    if (key.empty()) {
        if (error) error("invalid guid");
        return;
    }
    putAction(discoverApiBase, AppConfig::instance().getAccountToken(), then, error,
        add ? apiWatchlistAdd : apiWatchlistRemove, key);
}

/// Provider -> active server mapping: library items bearing this guid
/// (GET /library/all?guid=... verified). Empty Container = not in the
/// library; otherwise Items[0] is the SERVER item (local ratingKey).
inline void matchInLibrary(
    const std::string& guid, const std::function<void(Container<Item>)>& then, OnError error) {
    auto& conf = AppConfig::instance();
    getJSON<Container<Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiLibraryMatch, HTTP::encode_form({{"guid", guid}}));
}

/// Set of guids of the ENTIRE library of the active server (movie and show
/// sections) — O(1) lookup "is this watchlisted title on the server?".
///
/// Server findings (real server, 2026-06-10):
///  - the `guid` field (plex://movie|show/...) is present in the STANDARD
///    listing of /library/sections/{id}/all — no parameter required
///    (includeGuids only concerns the external Guid[] tags tmdb/imdb);
///  - volume: 6613 movies + 232 shows = 6845 guids; raw listing
///    ~2.7 KB/item (~18 MB total) -> response SLIMMED via excludeFields +
///    excludeElements (~0.5 KB/item, the Image and UltraBlurColors elements
///    cannot be excluded) and PAGINATED by 1000 to bound each parse;
///  - measured (curl, LAN): 6845 items / ~3.3 MB / ~3-5 s in 8 pages of 1000.
///
/// Everything is fetched inside ONE brls::async (synchronous getSync loops),
/// then `then(set)` is called back on the UI thread. On network failure,
/// `error` is called and the caller must degrade (no set = unknown presence).
inline void fetchLibraryGuids(
    const std::function<void(std::shared_ptr<std::unordered_set<std::string>>)>& then, OnError error) {
    auto& conf = AppConfig::instance();
    std::string base = conf.getUrl();
    std::string token = conf.getToken();
    brls::async([base, token, then, error]() {
        try {
            auto guids = std::make_shared<std::unordered_set<std::string>>();
            auto sections = getSync(base + std::string(apiSections), token).get<Container<Section>>();
            for (auto& section : sections.Items) {
                if (section.type != "movie" && section.type != "show") continue;
                const size_t pageSize = 1000;
                size_t start = 0;
                while (true) {
                    HTTP::Form query = {
                        // minimal response: only guid/ratingKey/type remain
                        // (plus Image/UltraBlurColors, which cannot be excluded)
                        {"excludeFields",
                            "summary,tagline,art,thumb,studio,contentRating,originalTitle,audienceRatingImage,"
                            "ratingImage,primaryExtraKey,key,originallyAvailableAt,addedAt,updatedAt,duration,"
                            "rating,audienceRating,year,title,titleSort,slug,theme,banner,index,childCount,"
                            "leafCount,viewedLeafCount,contentRatingAge,mediaCount,mediaCountOptimized"},
                        {"excludeElements", "Media,Genre,Country,Director,Writer,Role,Collection,Image,"
                                            "UltraBlurColors,Guid,Label"},
                    };
                    addPagination(query, start, pageSize);
                    auto r = getSync(base + fmt::format(fmt::runtime(apiSectionAll), section.key,
                                                HTTP::encode_form(query)),
                        token)
                                 .get<Container<Item>>();
                    for (auto& item : r.Items)
                        if (!item.guid.empty()) guids->insert(item.guid);
                    start += r.Items.size();
                    if (r.Items.empty() || (long)start >= r.TotalRecordCount) break;
                }
            }
            brls::sync(std::bind(then, std::move(guids)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

}  // namespace plex
