#include "tab/suggest_movie.hpp"
#include "view/recyling_video.hpp"
#include "api/plex.hpp"

SuggestMovie::SuggestMovie(const std::string id) : itemId(id) {
    // same layout as the show suggestions: hub rows
    this->inflateFromXMLRes("xml/tabs/suggest_show.xml");
    brls::Logger::debug("Tab SuggestMovie: create");
}

void SuggestMovie::onCreate() { this->doHubs(); }

void SuggestMovie::doHubs() {
    std::string query = HTTP::encode_form({{"count", "20"}});

    ASYNC_RETAIN
    // section hubs: replaces /Movies/Recommendations, which has no Plex
    // equivalent (PLEX_MIGRATION.md §2.5)
    plex::getJSON<plex::Container<plex::Hub>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            this->box->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                // landscape thumbnails for episodes/clips, posters otherwise
                if (hub.type == plex::mediaTypeEpisode || hub.type == plex::mediaTypeClip) {
                    row->setFrameHeight(brls::getStyle()["app/card/wide/row"]);
                    row->setItemWidth(brls::getStyle()["app/card/wide/width"]);
                } else {
                    row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
                    row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
                }
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, hub.title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
                this->box->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiHubsSection, this->itemId, query);
}
