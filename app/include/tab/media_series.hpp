/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;

class MediaSeries : public brls::Box {
public:
    MediaSeries(const std::string& id);

private:
    BRLS_BIND(brls::Label, labelSeason, "series/label/season");
    BRLS_BIND(brls::SelectorCell, selectorSeason, "series/selector/season");
    BRLS_BIND(RecyclingGrid, recyclerEpisodes, "media/episodes");

    void doSeasons();
    void doEpisodes(const std::string& seasonId);

    std::string seriesId;
    std::vector<std::string> seasonIds;
};
