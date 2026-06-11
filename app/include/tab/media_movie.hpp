/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <view/presenter.hpp>
#include <api/plex/types.hpp>
#include <utils/download.hpp>

class HRecyclerFrame;
class TextBox;
class IconButton;

class MediaMovie : public brls::Box, public Presenter {
public:
    MediaMovie(const plex::Item& item);
    ~MediaMovie() override;

private:
    BRLS_BIND(brls::Box, bannerBox, "movie/banner");
    BRLS_BIND(brls::Box, contentRow, "movie/content/row");
    BRLS_BIND(brls::Box, contentInfo, "movie/content/info");
    BRLS_BIND(brls::Image, imageBackdrop, "movie/image/backdrop");
    BRLS_BIND(brls::Image, imageFade, "movie/banner/fade");
    BRLS_BIND(brls::Image, imageLogo, "movie/image/logo");
    BRLS_BIND(brls::Image, imagePoster, "movie/image/poster");
    BRLS_BIND(brls::Label, labelTitle, "movie/label/title");
    BRLS_BIND(brls::Label, labelYear, "movie/label/year");
    BRLS_BIND(brls::Label, parentalRating, "movie/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "movie/label/rating");
    BRLS_BIND(TextBox, labelOverview, "movie/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "movie/label/genres");
    BRLS_BIND(brls::SelectorCell, btnSource, "movie/source");
    BRLS_BIND(IconButton, btnPlay, "movie/play");
    BRLS_BIND(IconButton, btnDownload, "movie/download");
    BRLS_BIND(IconButton, btnWatchlist, "movie/watchlist");
    BRLS_BIND(HRecyclerFrame, people, "movie/people");
    BRLS_BIND(brls::Box, boxRelated, "movie/related/box");

    void doRequest() override;
    void doMovie();
    void doRelated();
    void updateDownloadButton();
    /// reveals the Watchlist button once the provider state is known (plex:// guid)
    void initWatchlist(const std::string& guid);
    void toggleWatchlist();
    void updateWatchlistButton();

    DownloadManager::ProgressEvent::Subscription progressSub;
    DownloadManager::StatusEvent::Subscription statusSub;

    int64_t viewOffsetMs = 0;
    std::string itemId;
    std::string itemGuid;
    bool watchlisted = false;
    /// selected version (item.media[]) — no effect in v1 (playback = first
    /// accessible version, cf. activity/player_view.cpp)
    size_t selectedVersion = 0;
};
