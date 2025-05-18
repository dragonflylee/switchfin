/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>

class HRecyclerFrame;

class MediaMovie : public brls::Box {
public:
    MediaMovie(const jellyfin::Item& item);
    ~MediaMovie() override;

private:
    BRLS_BIND(brls::Image, imageLogo, "movie/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "movie/header/title");
    BRLS_BIND(brls::Label, labelYear, "movie/label/year");
    BRLS_BIND(brls::Label, parentalRating, "movie/parental/rating");
    BRLS_BIND(brls::Label, labelRating, "movie/label/rating");
    BRLS_BIND(brls::Label, labelOverview, "movie/label/overview");
    BRLS_BIND(brls::Label, labelGenres, "movie/label/genres");
    BRLS_BIND(brls::Header, labelSimilar, "movie/label/similar");
    BRLS_BIND(brls::Button, btnPlay, "movie/play");
    BRLS_BIND(HRecyclerFrame, people, "movie/people");
    BRLS_BIND(HRecyclerFrame, similar, "movie/similar");

    void doMovie(const std::string& itemId);
    void doSimilar(const std::string& itemId);
};
