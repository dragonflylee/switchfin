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

class MediaMovie : public brls::Box, public Presenter {
public:
    MediaMovie(const plex::Item& item);
    ~MediaMovie() override;

private:
    BRLS_BIND(brls::Box, bannerBox, "movie/banner");
    BRLS_BIND(brls::Box, contentRow, "movie/content/row");
    BRLS_BIND(brls::Box, contentInfo, "movie/content/info");
    BRLS_BIND(brls::Image, imageBackdrop, "movie/image/backdrop");
    BRLS_BIND(brls::Image, imageLogo, "movie/image/logo");
    BRLS_BIND(brls::Image, imagePoster, "movie/image/poster");
    BRLS_BIND(brls::Header, headerTitle, "movie/header/title");
    BRLS_BIND(brls::Label, labelYear, "movie/label/year");
    BRLS_BIND(brls::Label, parentalRating, "movie/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "movie/label/rating");
    BRLS_BIND(TextBox, labelOverview, "movie/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "movie/label/genres");
    BRLS_BIND(brls::SelectorCell, btnSource, "movie/source");
    BRLS_BIND(brls::Button, btnPlay, "movie/play");
    BRLS_BIND(brls::Button, btnDownload, "movie/download");
    BRLS_BIND(HRecyclerFrame, people, "movie/people");
    BRLS_BIND(brls::Box, boxRelated, "movie/related/box");

    void doRequest() override;
    void doMovie();
    void doRelated();
    void updateDownloadButton();

    DownloadManager::ProgressEvent::Subscription progressSub;
    DownloadManager::StatusEvent::Subscription statusSub;

    int64_t viewOffsetMs = 0;
    std::string itemId;
    /// version sélectionnée (item.media[]) — sans effet v1 (lecture = première
    /// version accessible, cf. activity/player_view.cpp)
    size_t selectedVersion = 0;
};
