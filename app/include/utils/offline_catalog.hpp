#pragma once

/*
    Pure catalog logic for offline browsing (SPEC §4.1).

    Turns a flat set of plex::Item snapshots (persisted under downloads/meta/)
    into a navigable libraries -> movies/shows -> seasons -> episodes tree,
    mirroring the online model. Header-only and dependency-free (plex types +
    STL) so it can be unit-tested without Borealis/config — the OfflineLibrary
    singleton (I/O, singleton state) builds on top of it.
*/

#include <api/plex/types.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace offline {

/// Deterministic, filesystem-safe slug from a title (synthetic keys only).
inline std::string slug(const std::string& s) {
    std::string out;
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u))
            out += (char)std::tolower(u);
        else if (c == ' ' || c == '-' || c == '_')
            out += '-';
    }
    return out.empty() ? "x" : out;
}

/// Synthetic ancestor keys for legacy downloads that lack real ratingKeys.
inline std::string syntheticShowKey(const std::string& showTitle) { return "local:show:" + slug(showTitle); }
inline std::string syntheticSeasonKey(const std::string& showKey, int64_t seasonIndex) {
    return showKey + ":s" + std::to_string(seasonIndex);
}

/// A top-level (grid) node is a movie or a show.
inline bool isTopLevel(const plex::Item& it) {
    return it.type == plex::mediaTypeMovie || it.type == plex::mediaTypeShow;
}

/// Owning library key: the real librarySectionID, or a synthetic bucket by
/// media type so navigation still works when the field was not captured.
inline std::string sectionKeyOf(const plex::Item& it) {
    if (!it.librarySectionID.empty()) return it.librarySectionID;
    return it.type == plex::mediaTypeMovie ? "local:lib:movie" : "local:lib:show";
}

/// Ensure every episode has a show + season node in the set. Missing ancestors
/// are synthesized from the episode's grandparent*/parent* fields (and the
/// episode is re-linked to them) so a legacy download captured before rich
/// metadata is still browsable. Idempotent: real ancestors already present are
/// left untouched.
inline std::vector<plex::Item> synthesizeAncestors(std::vector<plex::Item> nodes) {
    std::unordered_set<std::string> have;
    for (auto& it : nodes) have.insert(it.ratingKey);

    std::vector<plex::Item> added;
    for (auto& it : nodes) {
        if (it.type != plex::mediaTypeEpisode) continue;

        std::string showKey = it.grandparentRatingKey;
        if (showKey.empty()) {
            showKey = syntheticShowKey(it.grandparentTitle);
            it.grandparentRatingKey = showKey;
        }
        std::string seasonKey = it.parentRatingKey;
        if (seasonKey.empty()) {
            seasonKey = syntheticSeasonKey(showKey, it.parentIndex);
            it.parentRatingKey = seasonKey;
        }

        if (!have.count(showKey)) {
            plex::Item show;
            show.ratingKey = showKey;
            show.type = plex::mediaTypeShow;
            show.title = it.grandparentTitle.empty() ? "Unknown" : it.grandparentTitle;
            show.thumb = it.grandparentThumb;
            show.art = it.grandparentArt;
            show.librarySectionID = it.librarySectionID;
            show.librarySectionTitle = it.librarySectionTitle;
            have.insert(showKey);
            added.push_back(std::move(show));
        }
        if (!have.count(seasonKey)) {
            plex::Item season;
            season.ratingKey = seasonKey;
            season.type = plex::mediaTypeSeason;
            season.parentRatingKey = showKey;
            season.index = it.parentIndex;
            season.title = it.parentTitle.empty() ? ("Season " + std::to_string(it.parentIndex)) : it.parentTitle;
            season.thumb = it.parentThumb.empty() ? it.grandparentThumb : it.parentThumb;
            season.librarySectionID = it.librarySectionID;
            season.librarySectionTitle = it.librarySectionTitle;
            have.insert(seasonKey);
            added.push_back(std::move(season));
        }
    }
    nodes.insert(nodes.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
    return nodes;
}

/// Distinct libraries (movies + shows) present, as plex::Section, sorted by title.
inline std::vector<plex::Section> buildSections(const std::vector<plex::Item>& nodes) {
    std::vector<plex::Section> out;
    std::unordered_set<std::string> seen;
    for (const auto& it : nodes) {
        if (!isTopLevel(it)) continue;
        std::string key = sectionKeyOf(it);
        if (!seen.insert(key).second) continue;
        plex::Section s;
        s.key = key;
        s.type = it.type == plex::mediaTypeMovie ? plex::mediaTypeMovie : plex::mediaTypeShow;
        s.title = !it.librarySectionTitle.empty() ? it.librarySectionTitle
                                                   : (it.type == plex::mediaTypeMovie ? "Movies" : "TV Shows");
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const plex::Section& a, const plex::Section& b) { return a.title < b.title; });
    return out;
}

/// Top-level items (movies + shows) of a section, sorted by title.
inline std::vector<plex::Item> sectionItems(const std::vector<plex::Item>& nodes, const std::string& sectionKey) {
    std::vector<plex::Item> out;
    for (const auto& it : nodes)
        if (isTopLevel(it) && sectionKeyOf(it) == sectionKey) out.push_back(it);
    std::sort(out.begin(), out.end(), [](const plex::Item& a, const plex::Item& b) { return a.title < b.title; });
    return out;
}

/// Direct children (seasons of a show, or episodes of a season), sorted by index.
inline std::vector<plex::Item> childrenOf(const std::vector<plex::Item>& nodes, const std::string& parentRatingKey) {
    std::vector<plex::Item> out;
    if (parentRatingKey.empty()) return out;
    for (const auto& it : nodes)
        if (it.parentRatingKey == parentRatingKey) out.push_back(it);
    std::sort(out.begin(), out.end(), [](const plex::Item& a, const plex::Item& b) { return a.index < b.index; });
    return out;
}

/// All episodes under a show, across seasons (mirrors /allLeaves), sorted by
/// (season index, episode index).
inline std::vector<plex::Item> leavesOf(const std::vector<plex::Item>& nodes, const std::string& showRatingKey) {
    std::vector<plex::Item> out;
    for (const auto& it : nodes)
        if (it.type == plex::mediaTypeEpisode && it.grandparentRatingKey == showRatingKey) out.push_back(it);
    std::sort(out.begin(), out.end(), [](const plex::Item& a, const plex::Item& b) {
        return a.parentIndex != b.parentIndex ? a.parentIndex < b.parentIndex : a.index < b.index;
    });
    return out;
}

}  // namespace offline
