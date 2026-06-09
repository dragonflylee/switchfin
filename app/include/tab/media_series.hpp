/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>
#include <view/presenter.hpp>

class AutoTabFrame;
class HRecyclerFrame;
class TextBox;

class MediaSeries : public brls::Box, public Presenter {
public:
    /// item de type "show" OU "season" (la série parente est alors résolue)
    MediaSeries(const plex::Item& item);
    ~MediaSeries() override;

    void doRequest() override;

private:
    BRLS_BIND(brls::Box, bannerBox, "series/banner");
    BRLS_BIND(brls::Box, contentRow, "series/content/row");
    BRLS_BIND(brls::Box, contentInfo, "series/content/info");
    BRLS_BIND(brls::Image, imageBackdrop, "series/image/backdrop");
    BRLS_BIND(brls::Image, imageLogo, "series/image/logo");
    BRLS_BIND(brls::Image, imagePoster, "series/image/poster");
    BRLS_BIND(brls::Header, headerTitle, "series/header/title");
    BRLS_BIND(brls::Label, labelYear, "series/label/year");
    BRLS_BIND(brls::Label, parentalRating, "series/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "series/label/rating");
    BRLS_BIND(TextBox, labelOverview, "series/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "series/label/genres");
    BRLS_BIND(brls::Header, labelNextup, "series/label/nextup");
    BRLS_BIND(brls::Header, labelSpecial, "series/label/special");
    BRLS_BIND(HRecyclerFrame, people, "series/people");
    BRLS_BIND(HRecyclerFrame, special, "series/special");
    BRLS_BIND(HRecyclerFrame, nextUp, "series/nextup");
    BRLS_BIND(brls::Box, boxRelated, "series/related/box");
    BRLS_BIND(AutoTabFrame, tabFrame, "series/tabFrame");

    void doSeries();
    void doSeason();
    void doRelated();
    void doNextup();
    void doSpecial();

    std::string seriesId;  // ratingKey de la série
};
