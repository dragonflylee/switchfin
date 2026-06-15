#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/svg_image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;

VideoCardCell::VideoCardCell() {
    this->inflateFromXMLRes("xml/view/video_card.xml");

    auto actionListener = [this](brls::View*) -> bool {
        brls::Box* view = this->getParent()->getParent();
        RecyclingView* recycler = dynamic_cast<RecyclingView*>(view);
        if (!recycler) return false;
        VideoDataSource* dataSrc = dynamic_cast<VideoDataSource*>(recycler->getDataSource());
        if (!dataSrc) return false;
        dataSrc->onContextMenu(this, this->getIndex());
        return true;
    };
    this->registerAction("hints/submit"_i18n, brls::BUTTON_X, actionListener, true);
    this->registerAction(KeyBind::getSetting(), actionListener);
}

void BaseCardCell::setWatched(bool played) {
    if (played) {
        this->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        this->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }
    // watched clears any resume bar; un-watched also hides it (offset reset)
    this->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
}