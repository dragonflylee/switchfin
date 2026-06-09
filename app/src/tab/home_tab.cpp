#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/plex.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

/// L'accueil reflète les rangées configurées côté serveur : « Continuer à
/// regarder » puis les hubs de /hubs, avec leurs titres localisés
/// (X-Plex-Language) — PLEX_MIGRATION.md §2.5.
void HomeTab::doRequest() {
    this->boxHome->clearViews();

    // Rangée « Continuer à regarder » — ajoutée tout de suite pour garantir
    // sa position en tête, remplie à la réponse (plex_client.dart:1421-1463)
    RecylingVideo* resume = new RecylingVideo();
    resume->setTitle("main/home/resume"_i18n);
    resume->setFrameHeight(300);
    resume->setItemWidth(175);
    resume->setVisibility(brls::Visibility::GONE);
    this->boxHome->addView(resume);

    this->doResume(resume);
    this->doHubs();
}

void HomeTab::doResume(RecylingVideo* row) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({{"count", "20"}});

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Hub>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN, row](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                if (!hub.title.empty()) row->setTitle(hub.title);
                row->setItems(hub.items);
                return;
            }
            row->setItems({});
        },
        [ASYNC_TOKEN, row](const std::string& ex) {
            ASYNC_RELEASE
            row->setItems({});
            brls::Logger::warning("home continueWatching: {}", ex);
        },
        plex::apiHubContinue, query);
}

void HomeTab::doHubs() {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({
        {"count", "20"},
        {"excludeContinueWatching", "1"},
    });

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Hub>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                // au cas où le serveur les renvoie malgré excludeContinueWatching
                if (hub.hubIdentifier == "home.continue" || hub.hubIdentifier == "home.ondeck") continue;

                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                row->setFrameHeight(300);
                row->setItemWidth(175);
                row->setItems(hub.items);
                this->boxHome->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            auto dialog = new brls::Dialog(ex);
            dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->doRequest(); }); });
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
        },
        plex::apiHubs, query);
}

void HomeTab::onCreate() {
    auto actionRefresh = [this](brls::View* view) {
        this->doRequest();
        return true;
    };

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}
