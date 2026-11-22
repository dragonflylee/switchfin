#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/event.hpp>

class RecyclingGrid;

class MusicAlbum : public brls::Box {
public:
    MusicAlbum(const jellyfin::Item& item);
    ~MusicAlbum() override;

private:
    BRLS_BIND(brls::Label, albumTitle, "album/label/title");
    BRLS_BIND(brls::Label, albumAritst, "album/label/artist");
    BRLS_BIND(brls::Label, albumYear, "album/label/year");
    BRLS_BIND(brls::Box, albumTrackCount, "album/tracks/count");
    BRLS_BIND(brls::Label, albumTracks, "album/label/tracks");
    BRLS_BIND(brls::Box, albumDurationBox, "album/duration");
    BRLS_BIND(brls::Label, albumDuration, "album/label/duration");
    BRLS_BIND(brls::Label, albumGenres, "album/label/genres");
    BRLS_BIND(brls::Image, imageCover, "album/image/cover");
    BRLS_BIND(RecyclingGrid, tracks, "album/tracks");
    BRLS_BIND(brls::Box, albumStats, "album/stats");

    void doAlbum();
    void doTracks();

    std::string itemId;
    brls::Box *prevParent = nullptr;
    MPVCustomEvent::Subscription customEventSubscribeID;
};