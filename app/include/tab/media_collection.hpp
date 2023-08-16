/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;

class MediaCollection : public brls::Box {
public:
    MediaCollection(const std::string& itemId, const std::string& itemType = "");

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recyclerSeries, "media/series");

    void doRequest();

    std::string itemId;
    std::string itemType;
    long pageSize;
    long startIndex;
};