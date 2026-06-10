/*
    pleNx — onglet sidebar « Listes de lecture ».
    Grille de TOUTES les playlists vidéo du serveur : cartes carrées
    (composite Plex = mosaïque 1:1) + titre + « N éléments ».
    API : GET /playlists?playlistType=video (plex::apiPlaylists),
    pagination X-Plex-Container-* ; clic → PlaylistView.
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
