/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

class HRecyclerFrame;

class RecylingVideo : public brls::Box {
public:
    RecylingVideo();
    ~RecylingVideo() override;

    static brls::View* create();

    using Callback = std::function<std::string(size_t, size_t)>;

    void reset() { this->start = 0; }
    void setTitle(const std::string& text);
    void setFrameHeight(float height);
    void setItemWidth(float width);
    /// side inset carried by the row itself: the title is indented,
    /// the cards scroll up to the edges (full-bleed scroll area)
    void setSidePadding(float padding);
    void setPageSize( size_t pageSize);
    void onQuery(const Callback& callback = nullptr);
    void doRequest(bool refresh = false);
    void doLatest(bool refresh = false);
    /// Feeds the row directly (hubs) without a request; GONE if empty.
    /// With moreTitle/moreKey (hub more=1): "+" card at the end of the row
    /// to the full hub page (HubView).
    void setItems(const std::vector<plex::Item>& items);
    void setItems(const std::vector<plex::Item>& items, const std::string& moreTitle, const std::string& moreKey);

private:
    BRLS_BIND(brls::Header, title, "recycler/title");
    BRLS_BIND(HRecyclerFrame, recycler, "recycler/videos");

    Callback queryCallback = nullptr;
    size_t start = 0;
    size_t pageSize = 10;
};