/*
    pleNx — "Playlists" sidebar tab.
    Grid of ALL the server's video playlists: square cards
    (Plex composite = 1:1 mosaic) + title + "N items".
    API: GET /playlists?playlistType=video (plex::apiPlaylists),
    X-Plex-Container-* pagination; click -> PlaylistView.
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;

class PlaylistsTab : public AttachedView {
public:
    PlaylistsTab();

    brls::View* getDefaultFocus() override;

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recycler, "playlists/grid");

    void doRequest();

    size_t startIndex = 0;
    size_t pageSize = 60;
};
