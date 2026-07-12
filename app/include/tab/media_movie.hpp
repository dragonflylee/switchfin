/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <view/presenter.hpp>
#include <api/plex/types.hpp>
#include <utils/download.hpp>
#include <view/svg_image.hpp>

class HRecyclerFrame;
class TextBox;
class IconButton;

class MediaMovie : public brls::Box, public Presenter {
public:
    /// localContext = opened from the offline downloads area (render locally
    /// even when a server is reachable, SPEC AC6)
    MediaMovie(const plex::Item& item, bool localContext = false);
    ~MediaMovie() override;

private:
    BRLS_BIND(brls::ScrollingFrame, scroll, "movie/scroll");
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
    BRLS_BIND(SVGImage, iconRating, "movie/rating/icon");
    BRLS_BIND(SVGImage, iconAudience, "movie/audience/icon");
    BRLS_BIND(brls::Label, labelAudience, "movie/label/audience");
    BRLS_BIND(TextBox, labelOverview, "movie/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "movie/label/genres");
    BRLS_BIND(brls::Box, noticeBox, "movie/notice");
    BRLS_BIND(brls::Box, sourcesBox, "movie/sources");
    BRLS_BIND(brls::SelectorCell, btnSource, "movie/source");
    BRLS_BIND(IconButton, btnPlay, "movie/play");
    BRLS_BIND(IconButton, btnDownload, "movie/download");
    BRLS_BIND(IconButton, btnWatchlist, "movie/watchlist");
    BRLS_BIND(brls::View, peopleHeader, "movie/label/people");
    BRLS_BIND(HRecyclerFrame, people, "movie/people");
    BRLS_BIND(brls::Box, boxRelated, "movie/related/box");

    void doRequest() override;
    void doMovie();
    /// renders the fiche from an Item — shared by the server and local-catalog
    /// (offline / downloaded) paths
    void applyMovie(const media::Item& item);
    void doRelated();
    void updateDownloadButton();
    /// Builds the inline Stremio source list (one row per source) and wires the
    /// Play button's enabled/muted state. No-op for single-file backends.
    void buildSources(const media::Item& item);
    /// Opens the player on a specific source row (item.media[mediaIndex]).
    void playSource(int mediaIndex);
    /// Queues a download of a specific source row (Stremio: X on a release line).
    void downloadSource(int mediaIndex);
    /// reveals the personal-list button (watchlist/favorite) once its state is known
    void initWatchlist(const media::Item& item);
    void toggleWatchlist();
    void updateWatchlistButton();

    DownloadManager::ProgressEvent::Subscription progressSub;
    DownloadManager::StatusEvent::Subscription statusSub;

    int64_t viewOffsetMs = 0;
    std::string itemId;
    media::Item movieItem;  // resolved detail (sources/title) — backs source playback
    bool hasPlayableSource = false;
    brls::View* firstSourceRow = nullptr;  // default focus target (Stremio release list)
    media::Item listItem;  // item backing the personal-list (watchlist/favorite) button
    bool localContext = false;  // opened from the offline downloads area
    bool watchlisted = false;
    /// selected version (item.media[]) — no effect in v1 (playback = first
    /// accessible version, cf. activity/player_view.cpp)
    size_t selectedVersion = 0;
};
