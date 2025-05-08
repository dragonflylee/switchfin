/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>

class AutoTabFrame;
class HRecyclerFrame;

class MediaSeries : public brls::Box {
public:
    MediaSeries(const jellyfin::Item& item);
    ~MediaSeries() override;

private:
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "series/header/title");
    BRLS_BIND(brls::Label, labelYear, "series/label/year");
    BRLS_BIND(brls::Label, parentalRating, "series/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "series/label/rating");
    BRLS_BIND(brls::Label, labelOverview, "series/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "series/label/genres");
    BRLS_BIND(HRecyclerFrame, people, "series/people");
    BRLS_BIND(HRecyclerFrame, similar, "series/similar");
    BRLS_BIND(AutoTabFrame, tabFrame, "series/tabFrame");

    void doSeries(const std::string& itemId);
    void doSeason(const std::string& itemId);
    void doSimilar(const std::string& itemId);
};
