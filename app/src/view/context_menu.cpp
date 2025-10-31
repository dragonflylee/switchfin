#include "view/context_menu.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;

ContextMenu::ContextMenu(const jellyfin::Item& item) : itemId(item.Id) {
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

    this->btnFavorite->registerClickAction([this](brls::View* view) { return this->doFavorite(); });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnFavorite));
    this->btnFavorite->setSelected(item.UserData.IsFavorite);

    this->btnMarkPlay->registerClickAction([this](brls::View* view) { return this->doPlayed(); });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->btnMarkPlay->setSelected(item.UserData.Played);
}

bool ContextMenu::doPlayed() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
            {"played", this->btnMarkPlay->getSelected()},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->btnMarkPlay->setSelected(r.Played);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiPlayedItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

bool ContextMenu::doFavorite() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
            {"isFavorite", this->btnFavorite->getSelected()},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->btnFavorite->setSelected(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}