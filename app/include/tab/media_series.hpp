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

/// Show page: a single scrollable view (banner + poster + info +
/// Play/Download buttons + seasons row) — no more side tabs.
/// Designed to be stacked via ui::presentDetail (sidebar visible).
class MediaSeries : public brls::Box, public Presenter {
public:
    /// item of type "show" OR "season" (the parent show is then resolved,
    /// and the wanted season is opened on top of the page once listed)
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
    /// plays the next unwatched episode (OnDeck); fully watched show:
    /// restarts the first episode from the beginning ("Replay")
    void doPlay();
    /// downloads the whole show (filtered allLeaves, confirmation dialog)
    void doDownloadSeries();
    /// reveals the Watchlist button once the provider state is known (plex:// guid)
    void initWatchlist(const std::string& guid);
    void toggleWatchlist();
    void updateWatchlistButton();

    std::string seriesId;  // ratingKey of the show
    std::string seriesGuid;
    bool watchlisted = false;
    /// ratingKey of the season to open on top of the page once seasons are
    /// listed (item of type "season" or "go to season")
    std::string wantedSeason;
    /// show summary: fallback for season headers without their own summary
    std::string seriesSummary;
    /// next episode to play (OnDeck); empty ratingKey = no OnDeck
    plex::Item onDeck;
    /// fully watched show: onDeck = first episode (allLeaves) and the
    /// "Play" button becomes "Replay" (forced playback from 0)
    bool replay = false;
};
