#pragma once

/*
    media — thin routing helpers that let the browsing/fiche views read from the
    local OfflineLibrary instead of the Plex server (SPEC §4.3).

    Deliberately NOT a full polymorphic abstraction: each view keeps its exact
    server call (online behaviour unchanged) and simply prepends a local branch
    guarded by these helpers. Lower churn, no risk of regressing the online path.
*/

#include <api/plex/types.hpp>
#include "utils/network_state.hpp"

namespace media {

/// Serve a fiche/grid from the local catalog rather than the server. True when
/// browsing fully offline, or when the view was opened from the offline
/// downloads area (localContext) — so the Downloads section always renders
/// locally, even online (SPEC AC6), WITHOUT hiding non-downloaded episodes when
/// the same title is opened from the online library.
inline bool preferLocal(bool localContext) { return NetworkState::isOffline() || localContext; }

/// Wrap items into the MediaContainer shape the views consume.
inline plex::Container<plex::Item> container(std::vector<plex::Item> items) {
    plex::Container<plex::Item> c;
    c.TotalRecordCount = (long)items.size();
    c.Items = std::move(items);
    return c;
}

}  // namespace media
