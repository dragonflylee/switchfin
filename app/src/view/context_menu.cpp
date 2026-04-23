#include "view/context_menu.hpp"
#include "api/jellyfin.hpp"
#include "utils/download.hpp"

using namespace brls::literals;

ContextMenu::ContextMenu(const jellyfin::Item& item) : itemId(item.Id), item(item) {
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

    auto& dm = DownloadManager::instance();
    if (item.Type == jellyfin::mediaTypeMovie || item.Type == jellyfin::mediaTypeEpisode ||
        item.Type == jellyfin::mediaTypeVideo) {
        if (dm.isDownloaded(item.Id)) {
            this->btnDownload->title->setText("main/download/completed"_i18n);
            this->btnDownload->setSelected(true);
        } else if (dm.isDownloading(item.Id)) {
            this->btnDownload->title->setText("main/download/downloading"_i18n);
            this->btnDownload->setSelected(true);
        }
        this->btnDownload->registerClickAction([this](brls::View* view) {
            auto& dm = DownloadManager::instance();
            if (dm.isDownloaded(this->item.Id)) {
                brls::Application::notify("main/download/completed"_i18n);
            } else if (dm.isDownloading(this->item.Id)) {
                brls::Application::notify("main/download/downloading"_i18n);
            } else {
                int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
                dm.addDownload(this->item, static_cast<DownloadQuality>(qi));
                brls::Application::notify("main/download/queued"_i18n);
                this->btnDownload->setSelected(true);
            }
            return true;
        });
        this->btnDownload->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnDownload));
    } else {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
    }
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