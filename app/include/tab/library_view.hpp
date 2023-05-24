/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;

class LibraryView : public brls::Box {
public:
    LibraryView(const std::string& id);

private:
    BRLS_BIND(RecyclingGrid, recyclerSeries, "media/items");

    void doRequest();

    std::string itemId;
    long pageSize;
    long startIndex;
};