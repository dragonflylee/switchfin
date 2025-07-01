#include "view/video_card.hpp"
#include "view/context_menu.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;

VideoCardCell::VideoCardCell() {
    this->inflateFromXMLRes("xml/view/video_card.xml");

    this->registerAction(
        "hints/submit"_i18n, brls::BUTTON_X,
        [this](...) {
            if (this->id.empty()) return false;
            auto menu = new ContextMenu(this->id);
            brls::Application::pushActivity(new brls::Activity(menu));
            return true;
        },
        true);
}