#include "view/selector_cell.hpp"
#include "utils/dialog.hpp"

SelectorCell::SelectorCell() {
    this->registerClickAction([this](View* view) {
        brls::Dropdown* dropdown = new brls::Dropdown(
            this->title->getFullText(), data, [this](int selected) { this->setSelection(selected, false); }, selection,
            [this](int selected) { this->dismissEvent.fire(selected); });
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });

    this->registerBoolXMLAttribute("quitApp", [this](bool value) {
        if (value) this->dismissEvent.subscribe([](int selected) { Dialog::quitApp(); });
    });
}

brls::View* SelectorCell::create() { return new SelectorCell(); }