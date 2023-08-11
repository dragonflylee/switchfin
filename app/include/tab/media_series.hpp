/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "api/jellyfin/media.hpp"
#include "view/presenter.h"

class RecyclingGrid;

class MediaSeries : public brls::Box, public Presenter {
public:
    MediaSeries(const jellyfin::MediaItem& item);

    void doRequest() override;

private:
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::SelectorCell, selectorSeason, "series/selector/season");
    BRLS_BIND(RecyclingGrid, recyclerEpisodes, "media/episodes");

    void doSeason();
    void doEpisodes(const std::string& seasonId);

    std::string seriesId;
    std::vector<std::string> seasonIds;
};
