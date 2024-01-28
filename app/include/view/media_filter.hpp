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
    inline static bool selectedPlayed = false;
    inline static bool selectedUnplayed = false;

    inline static std::string sortList[] = {
        "SortName",
        "DateCreated",
        "DatePlayed",
        "PremiereDate",
        "PlayCount",
        "CommunityRating",
        "Random",
    };

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "media/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "media/sort/order");
    BRLS_BIND(brls::BooleanCell, filterPlayed, "media/filter/played");
    BRLS_BIND(brls::BooleanCell, filterUnplayed, "media/filter/unplayed");

    brls::VoidEvent event;
};