/*
    pleNx — vue d'UNE liste de lecture Plex.
    En-tête sobre (titre + « N éléments · durée totale ») et grille des
    éléments dans l'ordre serveur de la playlist (pas de tri/filtres).
    API : GET /playlists/{ratingKey}/items (plex::apiPlaylistItems),
    pagination X-Plex-Container-* comme MediaCollection.
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>

class RecyclingGrid;

class PlaylistView : public AttachedView {
public:
    /// @param item Metadata de type "playlist" (hub home, grille PlaylistsTab) :
    ///             ratingKey + title + leafCount/duration pour l'en-tête.
    explicit PlaylistView(const plex::Item& item);

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "playlist/items");

    /// labels de l'en-tête scrollé (xml/view/playlist_header.xml, possédé
    /// par la grille via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    /// « N éléments · X h YY » (durée omise si inconnue, label masqué si N <= 0)
    void updateMeta(int64_t count, int64_t durationMs);

    std::string playlistId;
    /// leafCount connu à la construction ; 0 → complété par totalSize à la 1re page
    int64_t knownCount = 0;
    size_t pageSize;
    size_t startIndex = 0;
};
