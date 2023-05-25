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
    BRLS_BIND(RecyclingGrid, recyclerSeason, "media/season");

    void doRequest();

    std::string itemId;
};
