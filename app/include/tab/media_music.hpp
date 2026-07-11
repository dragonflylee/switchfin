/*
    GMCA — music detail views (SPEC.md — issue #11).
    Artist -> Album -> Track mirrors Show -> Season -> Episode; getChildren is
    already generic, so these are thin views over it.
*/

#pragma once

#include <borealis.hpp>
#include <api/media/types.hpp>
#include <view/presenter.hpp>

class RecyclingGrid;

/// Artist page: a square grid of the artist's albums (getChildren(artistId)).
/// Stacked over the library grid via ui::presentDetail.
class MediaArtist : public brls::Box, public Presenter {
public:
    explicit MediaArtist(const media::Item& item);

    void doRequest() override;

private:
    void shufflePlay();  // fetch all the artist's tracks and play them shuffled

    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;
    media::Item artist;
};

/// Album page: header cell (cover + info + Play) then the track list
/// (getChildren(albumId)). Single-column flow, like MediaSeason.
class MediaAlbum : public brls::Box, public Presenter {
public:
    explicit MediaAlbum(const media::Item& item);

    void doRequest() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "music/tracks");
    media::Item album;
};
