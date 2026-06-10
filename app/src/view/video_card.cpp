#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;

VideoCardCell::VideoCardCell() {
    this->inflateFromXMLRes("xml/view/video_card.xml");

    auto actionListener = [this](brls::View*) -> bool {
        // remonte la hiérarchie jusqu'au recycler (RecyclingGrid vertical ou
        // HRecyclerFrame des rangées d'accueil) plutôt que de figer la
        // profondeur (getParent()->getParent()), fragile entre conteneurs
        brls::Box* view = this->getParent();
        RecyclingView* recycler = nullptr;
        while (view && !(recycler = dynamic_cast<RecyclingView*>(view))) view = view->getParent();
        if (!recycler) return false;
        VideoDataSource* dataSrc = dynamic_cast<VideoDataSource*>(recycler->getDataSource());
        if (!dataSrc) return false;
        dataSrc->onContextMenu(view, this->getIndex());
        return true;
    };
    // hint visible (« X Options ») dans la barre du bas quand la carte est
    // focusée — l'action était auparavant cachée (hidden=true), introuvable
    // manette en main sur console
    this->registerAction("hints/option"_i18n, brls::BUTTON_X, actionListener);
    this->registerAction(KeyBind::getSetting(), actionListener);
}
