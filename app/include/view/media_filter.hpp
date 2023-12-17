//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>

class MediaFilter : public brls::Box {
public:
    MediaFilter(std::function<void(void)> cb);
    ~MediaFilter() override;

    bool isTranslucent() override { return true; }

    inline static int selectedSort = 1;
    inline static int selectedOrder = 1;
    inline static bool selectedPlayed = false;
    inline static bool selectedUnplayed = false;

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "media/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "media/sort/order");
    BRLS_BIND(brls::BooleanCell, filterPlayed, "media/filter/played");
    BRLS_BIND(brls::BooleanCell, filterUnplayed, "media/filter/unplayed");
};