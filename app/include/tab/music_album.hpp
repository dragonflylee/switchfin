#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/event.hpp>

class RecyclingGrid;

class MusicAlbum : public brls::Box {
public:
    MusicAlbum(const jellyfin::MediaItem& item);
    ~MusicAlbum() override;

private:
    BRLS_BIND(brls::Label, albumTitle, "album/label/title");
    BRLS_BIND(brls::Label, albumAritst, "album/label/artist");
    BRLS_BIND(brls::Label, albumYear, "album/label/year");
    BRLS_BIND(brls::Image, imageCover, "album/image/cover");
    BRLS_BIND(RecyclingGrid, albumTracks, "album/tracks");
    BRLS_BIND(brls::Box, albumStats, "album/stats");

    void doAlbum();
    void doTracks();

    std::string itemId;
    MPVCustomEvent::Subscription customEventSubscribeID;
};