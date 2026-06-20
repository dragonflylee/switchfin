#include "tab/suggest_show.hpp"
#include "view/recyling_video.hpp"
#include "view/loading_spinner.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"

SuggestShow::SuggestShow(const std::string& id) : itemId(id) {
    this->inflateFromXMLRes("xml/tabs/suggest_show.xml");
    this->spinner = new LoadingSpinner();
    this->addView(this->spinner);
    brls::Logger::debug("Tab SuggestShow: create");
}

SuggestShow::~SuggestShow() { brls::Logger::debug("Tab SuggestShow: delete"); }

void SuggestShow::onCreate() { this->doRequest(); }

void SuggestShow::doRequest() {
    this->spinner->setSpinning(true);
    ASYNC_RETAIN
    AppConfig::instance().backend().getSectionHubs(this->itemId, 20,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
            ASYNC_RELEASE
            this->spinner->setSpinning(false);
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
            this->spinner->setSpinning(false);
            brls::Application::notify(ex);
        });
}
