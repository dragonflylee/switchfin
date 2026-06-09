#include "view/context_menu.hpp"
#include "api/plex.hpp"
#include "utils/download.hpp"

using namespace brls::literals;

ContextMenu::ContextMenu(const plex::Item& item) : itemId(item.ratingKey) {
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

    this->btnMarkPlay->registerClickAction([this](brls::View* view) {
        if (this->btnMarkPlay->getSelected())
            return this->unPlayed();
        else
            return this->doPlayed();
    });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->btnMarkPlay->setSelected(item.played());

    auto& dm = DownloadManager::instance();
    if (item.type == plex::mediaTypeMovie || item.type == plex::mediaTypeEpisode ||
        item.type == plex::mediaTypeClip) {
        if (dm.isDownloaded(item.ratingKey)) {
            this->btnDownload->title->setText("main/download/completed"_i18n);
            this->btnDownload->setSelected(true);
        } else if (dm.isDownloading(item.ratingKey)) {
            this->btnDownload->title->setText("main/download/downloading"_i18n);
            this->btnDownload->setSelected(true);
        }
        this->btnDownload->registerClickAction([this](brls::View* view) {
            auto& dm = DownloadManager::instance();
            if (dm.isDownloaded(this->itemId)) {
                brls::Application::notify("main/download/completed"_i18n);
            } else if (dm.isDownloading(this->itemId)) {
                brls::Application::notify("main/download/downloading"_i18n);
            } else {
                dm.addDownload(this->itemId);
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
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // GET /:/scrobble (plex_client.dart:1681-1688)
    plex::getAction(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        plex::apiScrobble, this->itemId);
    this->btnMarkPlay->setSelected(true);
    return true;
}

bool ContextMenu::unPlayed() {
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // GET /:/unscrobble (plex_client.dart:1693-1700)
    plex::getAction(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        plex::apiUnscrobble, this->itemId);
    this->btnMarkPlay->setSelected(false);
    return true;
}
