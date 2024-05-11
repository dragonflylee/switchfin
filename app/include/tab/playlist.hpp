#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>

class RecyclingGrid;

class Playlist : public brls::Box {
public:
    Playlist(const jellyfin::MediaItem& item);
    ~Playlist() override;

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(brls::Label, title, "album/label/title");
    BRLS_BIND(brls::Label, subtitle, "album/label/artist");
    BRLS_BIND(brls::Label, misc, "album/label/year");
    BRLS_BIND(brls::Image, cover, "album/image/cover");
    BRLS_BIND(RecyclingGrid, playlist, "album/tracks");
    BRLS_BIND(brls::Box, stats, "album/stats");

    void doList();

    std::string itemId;
};