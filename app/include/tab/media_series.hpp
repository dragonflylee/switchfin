/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "api/jellyfin/media.hpp"

class RecyclingGrid;

class MediaSeries : public brls::Box {
public:
    MediaSeries(jellyfin::MediaSeries& item);

private:
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::SelectorCell, selectorSeason, "series/selector/season");
    BRLS_BIND(RecyclingGrid, recyclerEpisodes, "media/episodes");

    void doSeasons();
    void doEpisodes(const std::string& seasonId);

    std::string seriesId;
    std::vector<std::string> seasonIds;
};
