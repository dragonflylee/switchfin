/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;

class MediaCollection : public brls::Box {
public:
    MediaCollection(const std::string& id);

private:
    BRLS_BIND(RecyclingGrid, recyclerSeries, "media/series");

    void doRequest();

    std::string itemId;
    long pageSize;
    long startIndex;
};