/*
    pleNx — onglet sidebar « Watchlist » (compte plex.tv).
    Grille d'affiches 2:3 des films/séries watchlistés, plus récent en premier.
    API : GET discover.provider.plex.tv/library/sections/watchlist/all
    (api/plex/watchlist.hpp — token COMPTE), pagination X-Plex-Container-*.
    Les items viennent du PROVIDER (peuvent ne pas exister sur le serveur) :
    le clic fait la correspondance par guid (plex::matchInLibrary) et ouvre la
    fiche locale si trouvée, sinon notifie « pas en bibliothèque ».

    Tri/filtres (action Y, panneau WatchlistFilter — watchlist_tab.cpp) :
    tri provider (watchlistedAt/titleSort/originallyAvailableAt, asc/desc),
    filtre Type provider (type=1|2), filtre Disponibilité CLIENT appuyé sur
    le cache des guid de la bibliothèque (plex::fetchLibraryGuids) — ce même
    cache sert à griser les affiches absentes du serveur.
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

    /// Remet la pagination à zéro et recharge la grille ; `reloadGuids`
    /// rafraîchit aussi le cache des guid de la bibliothèque (chargement
    /// initial et action « rafraîchir » — pas un simple changement de tri).
    void refresh(bool reloadGuids);
    void doRequest();

    /// guid (plex://…) présents sur le serveur actif — nullptr tant que
    /// fetchLibraryGuids n'a pas répondu (ou a échoué) : présence inconnue,
    /// pas de grisage ni de filtre Disponibilité effectif.
    std::shared_ptr<std::unordered_set<std::string>> libraryGuids;

    /// un DataSource a été posé pour le chargement en cours (les pages dont
    /// tous les items sont filtrés côté client n'en posent pas)
    bool loaded = false;

    size_t startIndex = 0;
    size_t pageSize = 60;
};
