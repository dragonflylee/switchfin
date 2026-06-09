#include "tab/suggest_show.hpp"
#include "view/recyling_video.hpp"
#include "api/plex.hpp"

SuggestShow::SuggestShow(const std::string& id) : itemId(id) {
    this->inflateFromXMLRes("xml/tabs/suggest_show.xml");
    brls::Logger::debug("Tab SuggestShow: create");
}

SuggestShow::~SuggestShow() { brls::Logger::debug("Tab SuggestShow: delete"); }

void SuggestShow::onCreate() { this->doRequest(); }

void SuggestShow::doRequest() {
    std::string query = HTTP::encode_form({{"count", "20"}});

    ASYNC_RETAIN
    // hubs de section : remplace Resume/NextUp/Latest scopés Jellyfin
    // (plex_client.dart:1918-1951)
    plex::getJSON<plex::Container<plex::Hub>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            this->box->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                // vignettes paysage pour les épisodes/clips, affiches sinon
                if (hub.type == plex::mediaTypeEpisode || hub.type == plex::mediaTypeClip) {
                    row->setFrameHeight(235);
                    row->setItemWidth(325);
                } else {
                    row->setFrameHeight(300);
                    row->setItemWidth(175);
                }
                row->setItems(hub.items);
                this->box->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiHubsSection, this->itemId, query);
}
