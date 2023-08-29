#pragma once

#include <borealis.hpp>

class RecyclingGrid;

class MusicAlbum : public brls::Box {
public:
    MusicAlbum(const std::string& itemId);
    ~MusicAlbum() override;

private:
    BRLS_BIND(brls::Header, albumTitle, "album/header/title");
    BRLS_BIND(brls::Image, imageCover, "album/image/cover");
    BRLS_BIND(RecyclingGrid, albumTracks, "album/tracks");

    void doAlbum();
    void doTracks();

    std::string albumId;
};