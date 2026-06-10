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
    /// retrait latéral porté par la rangée elle-même : le titre est indenté,
    /// les cartes défilent jusqu'aux bords (zone de scroll full-bleed)
    void setSidePadding(float padding);
    void setPageSize( size_t pageSize);
    void onQuery(const Callback& callback = nullptr);
    void doRequest(bool refresh = false);
    void doLatest(bool refresh = false);
    /// Alimente directement la rangée (hubs) sans requête ; GONE si vide.
    /// Avec moreTitle/moreKey (hub more=1) : carte « + » en fin de rangée
    /// vers la page complète du hub (HubView).
    void setItems(const std::vector<plex::Item>& items);
    void setItems(const std::vector<plex::Item>& items, const std::string& moreTitle, const std::string& moreKey);

private:
    BRLS_BIND(brls::Header, title, "recycler/title");
    BRLS_BIND(HRecyclerFrame, recycler, "recycler/videos");

    Callback queryCallback = nullptr;
    size_t start = 0;
    size_t pageSize = 10;
};