#pragma once

/*
    media — thin routing helpers that let the browsing/fiche views read from the
    local OfflineLibrary instead of the Plex server (SPEC §4.3).

    Deliberately NOT a full polymorphic abstraction: each view keeps its exact
    server call (online behaviour unchanged) and simply prepends a local branch
    guarded by these helpers. Lower churn, no risk of regressing the online path.
*/

#include <api/plex/types.hpp>
#include "utils/offline_library.hpp"
#include "utils/network_state.hpp"

namespace media {

/// Serve this item from the local catalog: offline globally, or the item is
/// downloaded (so a downloaded fiche renders locally even online — SPEC AC6).
inline bool preferLocal(const std::string& itemId) {
    return NetworkState::isOffline() || OfflineLibrary::instance().hasItem(itemId);
}

/// Wrap items into the MediaContainer shape the views consume.
inline plex::Container<plex::Item> container(std::vector<plex::Item> items) {
    plex::Container<plex::Item> c;
    c.TotalRecordCount = (long)items.size();
    c.Items = std::move(items);
    return c;
}

}  // namespace media
