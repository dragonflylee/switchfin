#include "view/context_menu.hpp"
#include "view/download_dialog.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;

ContextMenu::ContextMenu(const jellyfin::Item& item) : itemId(item.Id), itemName(item.Name) {
    this->inflateFromXMLRes("xml/view/context_menu.xml");
    brls::Logger::debug("ContextMenu: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    this->btnFavorite->registerClickAction([this](brls::View* view) {
        if (this->btnFavorite->getSelected())
            return this->unFavorite();
        else
            return this->doFavorite();
    });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnFavorite));
    this->btnFavorite->setSelected(item.UserData.IsFavorite);

    this->btnMarkPlay->registerClickAction([this](brls::View* view) {
        if (this->btnMarkPlay->getSelected())
            return this->unPlayed();
        else
            return this->doPlayed();
    });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->btnMarkPlay->setSelected(item.UserData.Played);

    this->btnDownload->registerClickAction([this](brls::View* view) {
        return this->doDownload();
    });
    this->btnDownload->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnDownload));
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

bool ContextMenu::unPlayed() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->btnMarkPlay->setSelected(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiPlayedItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

bool ContextMenu::unFavorite() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
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

bool ContextMenu::doDownload() {
    std::string id = this->itemId;
    std::string name = this->itemName;
    jellyfin::postJSON(
        {
            {"UserId", AppConfig::instance().getUserId()},
            {"MediaSourceId", this->itemId},
        },
        [id, name](const jellyfin::PlaybackResult& r) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [id, name, r]() {
                brls::Box* dialog = new DownloadDialog(id, name, r);
                brls::Application::pushActivity(new brls::Activity(dialog));
            });
        },
        [](const std::string& ex) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiPlayback, this->itemId);

    return true;
}