/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <view/presenter.hpp>

class RecyclingGrid;

class MediaSeries : public brls::Box, public Presenter {
public:
    MediaSeries(const std::string& itemId);
    ~MediaSeries() override;

    void doRequest() override;

private:
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "series/header/title");
    BRLS_BIND(brls::Label, labelYear, "series/label/year");
    BRLS_BIND(brls::Label, parentalRating, "series/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "series/label/rating");
    BRLS_BIND(brls::Label, labelOverview, "series/label/overview");
    BRLS_BIND(brls::SelectorCell, selectorSeason, "series/selector/season");
    BRLS_BIND(RecyclingGrid, recyclerEpisodes, "media/episodes");

    void doSeries();
    void doSeason();
    void doEpisodes(const std::string& seasonId);

    std::string seriesId;
    std::vector<std::string> seasonIds;
};
