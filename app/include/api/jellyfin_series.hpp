#pragma once

#include "jellyfin/media.hpp"
#include <unordered_map>
#include <unordered_set>

namespace jellyfin {

inline std::string parentSeriesId(const Episode& item) {
    if (item.Type == mediaTypeSeries) return item.Id;
    if ((item.Type == mediaTypeSeason || item.Type == mediaTypeEpisode) && item.SeriesId.is_string())
        return item.SeriesId.get<std::string>();
    return {};
}

// Latest is an activity feed, not a list of series: even grouped responses can
// contain seasons and episodes. Resolve real parent records before presenting
// series cards so navigation, artwork and context actions share one identity.
// fetchItems accepts a batch of IDs and returns user-scoped item records.
template <typename FetchItems>
std::vector<Episode> latestSeries(std::vector<Episode> latest, FetchItems fetchItems) {
    std::vector<std::string> missing;
    std::unordered_set<std::string> seen;
    for (const auto& item : latest) {
        if ((item.Type == mediaTypeSeason || item.Type == mediaTypeEpisode) &&
            parentSeriesId(item).empty() && !item.Id.empty() && seen.insert(item.Id).second)
            missing.push_back(item.Id);
    }
    if (!missing.empty()) {
        std::unordered_map<std::string, Episode> resolved;
        for (auto& item : fetchItems(missing)) resolved.emplace(item.Id, std::move(item));
        for (auto& item : latest) {
            if (!parentSeriesId(item).empty()) continue;
            const auto found = resolved.find(item.Id);
            if (found != resolved.end()) item = found->second;
        }
    }

    std::vector<std::string> order;
    seen.clear();
    for (const auto& item : latest) {
        auto id = parentSeriesId(item);
        if (!id.empty() && seen.insert(id).second) order.push_back(std::move(id));
    }
    if (order.empty()) return {};

    std::unordered_map<std::string, Episode> series;
    for (auto& item : fetchItems(order)) {
        if (item.Type == mediaTypeSeries && !item.Name.empty() && seen.count(item.Id))
            series.emplace(item.Id, std::move(item));
    }
    std::vector<Episode> result;
    result.reserve(order.size());
    for (const auto& id : order) {
        auto found = series.find(id);
        // Deleted/inaccessible parents are omitted. Never fabricate a Series
        // from a Season: that would apply watched/favorite actions to the child.
        if (found != series.end()) result.push_back(std::move(found->second));
    }
    return result;
}

} // namespace jellyfin
