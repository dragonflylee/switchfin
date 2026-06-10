/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>
#include <view/presenter.hpp>

class HRecyclerFrame;
class TextBox;
class IconButton;

/// Fiche série : une seule vue scrollable (bannière + poster + infos +
/// boutons Lire/Télécharger + rangée de saisons) — plus d'onglets latéraux.
/// Pensée pour être empilée via ui::presentDetail (sidebar visible).
class MediaSeries : public brls::Box, public Presenter {
public:
    /// item de type "show" OU "season" (la série parente est alors résolue,
    /// et la saison voulue est ouverte par-dessus la fiche une fois listée)
    MediaSeries(const plex::Item& item);
    ~MediaSeries() override;

    void doRequest() override;

private:
    BRLS_BIND(brls::Box, bannerBox, "series/banner");
    BRLS_BIND(brls::Box, contentRow, "series/content/row");
    BRLS_BIND(brls::Box, contentInfo, "series/content/info");
    BRLS_BIND(brls::Image, imageBackdrop, "series/image/backdrop");
    BRLS_BIND(brls::Image, imageFade, "series/banner/fade");
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::Image, imagePoster, "series/image/poster");
    BRLS_BIND(brls::Label, labelTitle, "series/label/title");
    BRLS_BIND(brls::Label, labelYear, "series/label/year");
    BRLS_BIND(brls::Label, parentalRating, "series/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "series/label/rating");
    BRLS_BIND(TextBox, labelOverview, "series/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "series/label/genres");
    BRLS_BIND(IconButton, btnPlay, "series/play");
    BRLS_BIND(IconButton, btnDownload, "series/download");
    BRLS_BIND(IconButton, btnWatchlist, "series/watchlist");
    BRLS_BIND(brls::Header, labelSeasons, "series/label/seasons");
    BRLS_BIND(HRecyclerFrame, seasons, "series/seasons");
    BRLS_BIND(brls::Header, labelSpecial, "series/label/special");
    BRLS_BIND(HRecyclerFrame, people, "series/people");
    BRLS_BIND(HRecyclerFrame, special, "series/special");
    BRLS_BIND(brls::Box, boxRelated, "series/related/box");

    void doSeries();
    void doSeason();
    void doRelated();
    void doNextup();
    void doSpecial();
    /// lance le prochain épisode non vu (OnDeck) ; série entièrement vue :
    /// relance le premier épisode depuis le début (« Relancer »)
    void doPlay();
    /// télécharge la série entière (allLeaves filtré, dialogue de confirmation)
    void doDownloadSeries();
    /// révèle le bouton Watchlist une fois l'état provider connu (guid plex://)
    void initWatchlist(const std::string& guid);
    void toggleWatchlist();
    void updateWatchlistButton();

    std::string seriesId;  // ratingKey de la série
    std::string seriesGuid;
    bool watchlisted = false;
    /// ratingKey de la saison à ouvrir par-dessus la fiche une fois les
    /// saisons listées (item de type "season" ou « aller à la saison »)
    std::string wantedSeason;
    /// résumé de la série : repli des en-têtes de saison sans résumé propre
    std::string seriesSummary;
    /// prochain épisode à lire (OnDeck) ; ratingKey vide = pas d'OnDeck
    plex::Item onDeck;
    /// série entièrement vue : onDeck = premier épisode (allLeaves) et le
    /// bouton « Lire » devient « Relancer » (lecture forcée depuis 0)
    bool replay = false;
};
