#include "view/context_menu.hpp"

using namespace brls::literals;

ContextMenu::ContextMenu(const std::string& itemId) {
    this->inflateFromXMLRes("xml/view/context_menu.xml");
    brls::Logger::debug("ContextMenu: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->registerClickAction([this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    this->btnFavorite->registerClickAction([this](brls::View* view) { return this->onClick(); });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnFavorite));

    this->btnMarkPlay->registerClickAction([this](brls::View* view) { return this->onClick(); });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
}

bool ContextMenu::onClick() {
    brls::Application::popActivity(brls::TransitionAnimation::NONE, []() { brls::Application::notify("unimpl"); });
    return true;
}