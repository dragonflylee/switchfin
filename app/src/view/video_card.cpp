#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/svg_image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;

void VideoCardCell::setWatched(bool played) {
    if (played) {
        this->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        this->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }
    // watched clears any resume bar; un-watched also hides it (offset reset)
    this->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
}

VideoCardCell::VideoCardCell() {
    this->inflateFromXMLRes("xml/view/video_card.xml");

    auto actionListener = [this](brls::View*) -> bool {
        // climbs the hierarchy up to the recycler (vertical RecyclingGrid
        // or the home rows' HRecyclerFrame) rather than freezing the depth
        // (getParent()->getParent()), fragile across containers
        brls::Box* view = this->getParent();
        RecyclingView* recycler = nullptr;
        while (view && !(recycler = dynamic_cast<RecyclingView*>(view))) view = view->getParent();
        if (!recycler) return false;
        VideoDataSource* dataSrc = dynamic_cast<VideoDataSource*>(recycler->getDataSource());
        if (!dataSrc) return false;
        dataSrc->onContextMenu(view, this->getIndex());
        return true;
    };
    // visible hint ("X Options") in the bottom bar when the card is
    // focused — the action used to be hidden (hidden=true), undiscoverable
    // gamepad in hand on console
    this->registerAction("hints/option"_i18n, brls::BUTTON_X, actionListener);
    this->registerAction(KeyBind::getSetting(), actionListener);
}
