/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <view/presenter.hpp>
#include <utils/download.hpp>

class HRecyclerFrame;
class TextBox;
class IconButton;

class MediaSeries : public brls::Box, public Presenter {
public:
    MediaSeries(const jellyfin::Episode& item);
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
    BRLS_BIND(brls::Header, labelSimilar, "series/label/similar");
    BRLS_BIND(brls::Header, labelSpecial, "series/label/special");
    BRLS_BIND(HRecyclerFrame, people, "series/people");
    BRLS_BIND(HRecyclerFrame, similar, "series/similar");
    BRLS_BIND(HRecyclerFrame, special, "series/special");
    BRLS_BIND(brls::Box, boxRelated, "series/related/box");

    void doSeries();
    void doSeason();
    void doSimilar();
    void doSpecial();
    /// plays the next unwatched episode
    void doPlay();
    /// downloads the whole show (filtered allLeaves, confirmation dialog)
    void doDownloadSeries();
    void updateDownloadButton();

    std::string seriesId;
    DownloadManager::StatusEvent::Subscription statusSub;
};
