//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>

class MediaFilter : public brls::Box {
public:
    MediaFilter();
    ~MediaFilter() override;

    bool isTranslucent() override { return true; }

    brls::VoidEvent* getEvent() { return &this->event; }

    inline static int selectedSort = 1;
    inline static int selectedOrder = 1;
    inline static bool selectedUnplayed = false;

    /// Plex sort fields, aligned with the selector labels;
    /// descending order = ":desc" suffix
    inline static std::string sortList[] = {
        "titleSort",
        "addedAt",
        "lastViewedAt",
        "originallyAvailableAt",
        "viewCount",
        "rating",
        "random",
    };

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "media/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "media/sort/order");
    BRLS_BIND(brls::BooleanCell, filterUnplayed, "media/filter/unplayed");

    brls::VoidEvent event;
};