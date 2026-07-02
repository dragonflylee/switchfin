/*
    GMCA — view of ONE Plex playlist.
    Plain header (title + "N items · total duration") and grid of the
    items in the server order of the playlist (no sort/filters).
    API: GET /playlists/{ratingKey}/items (plex::apiPlaylistItems),
    X-Plex-Container-* pagination like MediaCollection.
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>

class RecyclingGrid;

class PlaylistView : public AttachedView {
public:
    /// @param item Metadata of type "playlist" (home hub, PlaylistsTab grid):
    ///             ratingKey + title + leafCount/duration for the header.
    explicit PlaylistView(const plex::Item& item);

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "playlist/items");

    /// labels of the scrolled header (xml/view/playlist_header.xml, owned
    /// by the grid via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    /// "N items · X h YY" (duration omitted if unknown, label hidden if N <= 0)
    void updateMeta(int64_t count, int64_t durationMs);

    std::string playlistId;
    /// leafCount known at construction; 0 -> completed by totalSize on the 1st page
    int64_t knownCount = 0;
    size_t pageSize;
    size_t startIndex = 0;
};
