//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>

struct FilterState {
    int sort = 1;
    int order = 1;
    bool played = false;
    bool unplayed = false;
    bool favorite = false;

    static inline const std::string sortList[] = {
        "SortName",
        "DateCreated",
        "DatePlayed",
        "PremiereDate",
        "PlayCount",
        "CommunityRating",
        "Random",
    };

    const std::string& getSortBy() const { return sortList[(size_t)sort]; }
    const char* getSortOrder() const { return order ? "Descending" : "Ascending"; }
};

class MediaFilter : public brls::Box {
public:
    explicit MediaFilter(const FilterState& state);
    ~MediaFilter() override;

    bool isTranslucent() override { return true; }

    brls::VoidEvent* getEvent() { return &this->event; }

    const FilterState& getState() const { return this->state; }

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "media/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "media/sort/order");
    BRLS_BIND(brls::BooleanCell, filterPlayed, "media/filter/played");
    BRLS_BIND(brls::BooleanCell, filterUnplayed, "media/filter/unplayed");
    BRLS_BIND(brls::BooleanCell, filterFavorite, "media/filter/favorite");

    FilterState state;
    bool closed = false;
    brls::VoidEvent event;
};