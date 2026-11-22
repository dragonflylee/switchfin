/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <view/presenter.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/download.hpp>

class HRecyclerFrame;
class TextBox;
class IconButton;

class MediaMovie : public brls::Box, public Presenter {
public:
    MediaMovie(const jellyfin::Item& item);
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
    BRLS_BIND(brls::Label, labelOriginalTitle, "movie/label/original_title");
    BRLS_BIND(brls::Label, labelYear, "movie/label/year");
    BRLS_BIND(brls::Box, pillDuration, "movie/duration");
    BRLS_BIND(brls::Label, labelDuration, "movie/label/duration");
    BRLS_BIND(brls::Box, pillParental, "movie/parental");
    BRLS_BIND(brls::Label, parentalRating, "movie/parental/rating");
    BRLS_BIND(brls::Box, pillRating, "movie/rating");
    BRLS_BIND(brls::Label, labelRating, "movie/label/rating");
    BRLS_BIND(TextBox, labelOverview, "movie/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "movie/label/genres");
    BRLS_BIND(brls::Label, labelPlaybackTiming, "movie/label/playback_timing");
    BRLS_BIND(brls::Label, labelTagline, "movie/label/tagline");
    BRLS_BIND(brls::Label, labelMedia, "movie/label/media");
    BRLS_BIND(brls::Box, infoBox, "movie/info");
    BRLS_BIND(brls::Label, labelTags, "movie/info/tags");
    BRLS_BIND(brls::Box, boxProviders, "movie/info/providers");
    BRLS_BIND(brls::Label, labelProviders, "movie/info/providers/value");
    BRLS_BIND(brls::Box, boxStudios, "movie/info/studios");
    BRLS_BIND(brls::Label, labelStudios, "movie/info/studios/value");
    BRLS_BIND(brls::Box, boxRelease, "movie/info/release");
    BRLS_BIND(brls::Label, labelRelease, "movie/info/release/value");
    BRLS_BIND(brls::Box, boxDirector, "movie/info/director");
    BRLS_BIND(brls::Label, labelDirector, "movie/info/director/value");
    BRLS_BIND(brls::Box, boxWriter, "movie/info/writer");
    BRLS_BIND(brls::Label, labelWriter, "movie/info/writer/value");
    BRLS_BIND(brls::Header, labelPeople, "movie/label/people");
    BRLS_BIND(brls::Header, labelSimilar, "movie/label/similar");
    BRLS_BIND(brls::SelectorCell, btnSource, "movie/source");
    BRLS_BIND(IconButton, btnPlay, "movie/play");
    BRLS_BIND(IconButton, btnDownload, "movie/download");
    BRLS_BIND(IconButton, btnFavorite, "movie/favorite");
    BRLS_BIND(HRecyclerFrame, people, "movie/people");
    BRLS_BIND(HRecyclerFrame, similar, "movie/similar");
    BRLS_BIND(brls::Header, labelCrew, "movie/label/crew");
    BRLS_BIND(HRecyclerFrame, crew, "movie/crew");

    void doRequest() override;
    void doMovie();
    void doSimilar();
    void updateDownloadButton();
    void updatePlaybackTiming();
    void updateCredits(const jellyfin::Detail& r);

    bool doFavorite();
    bool unFavorite();
    void updateFavoriteButton(bool favorite);

    const uint64_t runTimeTicks;
    int64_t playTicks = 0;
    brls::RepeatingTimer playbackTimingTicker;
    std::string itemId;
    std::string sourceId;
    bool isFavorite = false;

    DownloadManager::ProgressEvent::Subscription progressSub;
    DownloadManager::StatusEvent::Subscription statusSub;
};
