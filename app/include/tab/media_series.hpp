/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <view/presenter.hpp>

class AutoTabFrame;
class HRecyclerFrame;
class TextBox;

class MediaSeries : public brls::Box, public Presenter {
public:
    MediaSeries(const jellyfin::Item& item);
    ~MediaSeries() override;

    void doRequest() override;

private:
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "series/header/title");
    BRLS_BIND(brls::Label, labelYear, "series/label/year");
    BRLS_BIND(brls::Label, parentalRating, "series/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "series/label/rating");
    BRLS_BIND(TextBox, labelOverview, "series/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "series/label/genres");
    BRLS_BIND(brls::Header, labelNextup, "series/label/nextup");
    BRLS_BIND(brls::Header, labelSimilar, "series/label/similar");
    BRLS_BIND(HRecyclerFrame, people, "series/people");
    BRLS_BIND(HRecyclerFrame, similar, "series/similar");
    BRLS_BIND(HRecyclerFrame, nextUp, "series/nextup");
    BRLS_BIND(AutoTabFrame, tabFrame, "series/tabFrame");

    void doSeries();
    void doSeason();
    void doSimilar();
    void doNextup();

    std::string seriesId;
};
