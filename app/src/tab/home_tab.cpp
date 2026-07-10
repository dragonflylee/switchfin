#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/plex.hpp"
#include "utils/keybind.hpp"
#include "utils/network_state.hpp"
#include "utils/offline_library.hpp"
#include "utils/offline_ui.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

/// The home screen mirrors the rows configured server-side: "Continue
/// watching" then the hubs from /hubs, with their localized titles
/// (X-Plex-Language) — PLEX_MIGRATION.md §2.5.
void HomeTab::doRequest() {
    // offline: the server hubs are unavailable — show one poster row per
    // downloaded library instead (same row layout as online) (SPEC AC13)
    if (NetworkState::isOffline()) {
        this->boxHome->clearViews();
        auto& lib = OfflineLibrary::instance();
        bool any = false;
        for (auto& s : lib.sections()) {
            auto items = lib.sectionItems(s.key);
            if (items.empty()) continue;
            RecylingVideo* row = new RecylingVideo();
            row->setTitle(s.title);
            row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
            row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
            row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
            row->setItems(items);
            this->boxHome->addView(row);
            any = true;
        }
        // nothing downloaded: offline empty state (icon + message + Retry)
        if (!any) this->boxHome->addView(offline_ui::makeEmpty());
        return;
    }

    // clearViews destroys the focused card when refreshing after playback:
    // remember to give the focus back to the first rebuilt row, otherwise it
    // falls back to the sidebar and the user loses track of it
    this->restoreFocus = hasFocusWithin(this);
    this->boxHome->clearViews();

    // "Continue watching" row — added right away to guarantee its position
    // at the top, filled when the response arrives
    RecylingVideo* resume = new RecylingVideo();
    resume->setTitle("main/home/resume"_i18n);
    resume->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
    resume->setItemWidth(brls::getStyle()["app/card/poster/width"]);
    resume->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
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
                std::string title = hub.title.empty() ? "main/home/resume"_i18n : hub.title;
                row->setTitle(title);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
                this->tryRestoreFocus();
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
                // in case the server returns them despite excludeContinueWatching
                if (hub.hubIdentifier == "home.continue" || hub.hubIdentifier == "home.ondeck") continue;

                // playlist hubs mix audio/photo/video: only video playlists
                // are playable in pleNx
                std::vector<plex::Item> items;
                items.reserve(hub.items.size());
                for (auto& item : hub.items) {
                    if (item.type == plex::mediaTypePlaylist && item.playlistType != "video") continue;
                    items.push_back(item);
                }
                if (items.empty()) continue;

                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                float frameHeight = brls::getStyle()["app/card/poster/row"];
                row->setFrameHeight(frameHeight);
                // playlists: SQUARE covers (custom poster or 1:1 composite)
                // — width = image height of the row (frame - 55 of labels,
                // video_card.xml metrics)
                bool playlists = !items.empty() && items.front().type == plex::mediaTypePlaylist;
                row->setItemWidth(playlists ? frameHeight - 55 : brls::getStyle()["app/card/poster/width"]);
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(items, hub.title, hub.key);
                } else {
                    row->setItems(items);
                }
                this->boxHome->addView(row);
            }
            this->tryRestoreFocus();
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

void HomeTab::tryRestoreFocus() {
    if (!this->restoreFocus) return;
    this->restoreFocus = false;
    // deferred one frame: the rows added by this callback are not laid out
    // yet, and giveFocus before the first layout fails silently
    ASYNC_RETAIN
    brls::sync([ASYNC_TOKEN]() {
        ASYNC_RELEASE
        brls::View* target = this->boxHome->getDefaultFocus();
        if (target) brls::Application::giveFocus(target);
    });
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
