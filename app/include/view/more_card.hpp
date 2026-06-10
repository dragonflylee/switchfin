/*
    pleNx — carte « + » de fin de rangée (hubs avec more=1) : ouvre la page
    complète du hub (HubView). Squelette aligné sur video_card.xml ; PAS un
    BaseCardCell (pas d'affiche ni de menu contextuel — et BaseCardCell
    résout "video/card/picture", absent ici).
*/

#pragma once

#include <view/recycling_grid.hpp>

class MoreCardCell : public RecyclingGridItem {
public:
    MoreCardCell() { this->inflateFromXMLRes("xml/view/more_card.xml"); }

    static RecyclingGridItem* create() { return new MoreCardCell(); }

    /// halo de focus sur la vignette seulement (contrat des cartes médias)
    brls::View* getDefaultFocus() override {
        brls::View* box = this->getView("more/card/box");
        if (box && box->isFocusable()) return box;
        return RecyclingGridItem::getDefaultFocus();
    }
};
