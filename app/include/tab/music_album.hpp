#pragma once

#include <borealis.hpp>

class RecyclingGrid;
class MusicView;

class MusicAlbum : public brls::Box {
public:
    MusicAlbum(const std::string& itemId);
    ~MusicAlbum() override;

private:
    BRLS_BIND(brls::Label, albumTitle, "album/label/title");
    BRLS_BIND(brls::Label, albumAritst, "album/label/artist");
    BRLS_BIND(brls::Label, albumYear, "album/label/year");
    BRLS_BIND(brls::Image, imageCover, "album/image/cover");
    BRLS_BIND(MusicView, musicView, "music/view");
    BRLS_BIND(RecyclingGrid, albumTracks, "album/tracks");

    void doAlbum();
    void doTracks();

    std::string albumId;
};