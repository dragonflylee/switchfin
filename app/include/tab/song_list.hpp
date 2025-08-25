/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>

class RecyclingGrid;

class SongList : public brls::Box {
public:
    SongList(const std::string& itemId, const std::string& artistIds = "");
    ~SongList() override;

private:
    BRLS_BIND(brls::Label, title, "album/label/title");
    BRLS_BIND(brls::Label, subtitle, "album/label/artist");
    BRLS_BIND(brls::Image, cover, "album/image/cover");
    BRLS_BIND(RecyclingGrid, list, "album/tracks");
    BRLS_BIND(brls::Box, stats, "album/stats");

    void doList();

    std::string itemId;
    std::string artistIds;
    size_t start = 0;
    size_t pageSize = 30;
    brls::Box *prevParent = nullptr;
    MPVCustomEvent::Subscription customEventSubscribeID;
};