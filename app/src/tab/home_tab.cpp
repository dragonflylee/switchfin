#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "view/loading_spinner.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");
    // centered spinner overlay shown while the home hubs load (the rows are
    // built into boxHome only once the response arrives).
    this->spinner = new LoadingSpinner();
    this->addView(this->spinner);
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

/// The home screen mirrors the rows configured server-side: "Continue
/// watching" then the hubs from /hubs, with their localized titles
/// (X-Plex-Language) — PLEX_MIGRATION.md §2.5.
void HomeTab::doRequest() {
    // clearViews destroys the focused card when refreshing after playback:
    // remember to give the focus back to the first rebuilt row, otherwise it
    // falls back to the sidebar and the user loses track of it
    this->restoreFocus = hasFocusWithin(this);
    this->spinner->setSpinning(true);
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
    ASYNC_RETAIN
    AppConfig::instance().backend().getContinueWatching(20,
        [ASYNC_TOKEN, row](const media::Container<media::Hub>& r) {
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
        });
}

void HomeTab::doHubs() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getHomeHubs(20, true,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
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
                // playlists AND music (artist/album/track): SQUARE covers (1:1)
                // — width = image height of the row (frame - 55 of labels,
                // video_card.xml metrics); everything else keeps 2:3 posters
                const std::string& t0 = items.front().type;
                bool square = t0 == plex::mediaTypePlaylist || t0 == plex::mediaTypeArtist ||
                              t0 == plex::mediaTypeAlbum || t0 == plex::mediaTypeTrack;
                row->setItemWidth(square ? frameHeight - 55 : brls::getStyle()["app/card/poster/width"]);
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(items, hub.title, hub.key);
                } else {
                    row->setItems(items);
                }
                this->boxHome->addView(row);
            }
            this->spinner->setSpinning(false);
            this->tryRestoreFocus();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->spinner->setSpinning(false);
            auto dialog = new brls::Dialog(ex);
            dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->doRequest(); }); });
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
        });
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
