/*
    GMCA — full page of a Plex hub (the "related" rows of detail pages:
    suggestions, collections, "More with..."). Opened by the "+" card at the
    end of a row when the server announces more=1.
    Scrolled header (title + "N items") and grid in server order;
    X-Plex-Container-* pagination on the hub key (which may already carry
    query parameters: /library/sections/2/all?actor=...).
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>

class RecyclingGrid;

class HubView : public AttachedView {
public:
    /// @param title hub title (localized by the server)
    /// @param key   relative hub path (Hub.key / hubKey)
    HubView(const std::string& title, const std::string& key);

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "hub/items");

    /// labels of the scrolled header (xml/view/grid_header.xml, owned by the
    /// grid via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    std::string hubKey;
    size_t pageSize;
    size_t startIndex = 0;
};
