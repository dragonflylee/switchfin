/*
    pleNx — end-of-row "+" card (hubs with more=1): opens the full hub page
    (HubView). Skeleton aligned with video_card.xml; NOT a BaseCardCell
    (no poster nor context menu — and BaseCardCell resolves
    "video/card/picture", absent here).
*/

#pragma once

#include <view/recycling_grid.hpp>

class MoreCardCell : public RecyclingGridItem {
public:
    MoreCardCell() { this->inflateFromXMLRes("xml/view/more_card.xml"); }

    static RecyclingGridItem* create() { return new MoreCardCell(); }

    /// focus halo on the thumbnail only (media cards contract)
    brls::View* getDefaultFocus() override {
        brls::View* box = this->getView("more/card/box");
        if (box && box->isFocusable()) return box;
        return RecyclingGridItem::getDefaultFocus();
    }
};
