#include "view/video_card.hpp"
#include "view/video_source.hpp"
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
        dataSrc->onContextMenu(view, this->getIndex());
        return true;
    };
    this->registerAction("hints/submit"_i18n, brls::BUTTON_X, actionListener, true);
    this->registerAction(KeyBind::getSetting(), actionListener);
}