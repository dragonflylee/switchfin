//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>

class MediaCollection;

class MediaFilter : public brls::Box {
public:
    MediaFilter(MediaCollection *view);
    ~MediaFilter() override;

    bool isTranslucent() override { return true; }

private:
    BRLS_BIND(brls::SelectorCell, sortBy, "media/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "media/sort/order");
    BRLS_BIND(brls::BooleanCell, filterPlayed, "media/filter/played");
    BRLS_BIND(brls::BooleanCell, filterUnplayed, "media/filter/unplayed");
};