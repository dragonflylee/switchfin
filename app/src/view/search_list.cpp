#include "view/search_list.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "api/plex.hpp"

SearchList::SearchList() {
    brls::Logger::debug("View SearchList: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/view/recycler_list.xml");

    this->registerStringXMLAttribute("title", [this](std::string value) { this->title->setTitle(value); });

    this->registerStringXMLAttribute("itemType", [this](std::string value) { this->itemType = value; });

    this->registerFloatXMLAttribute("pageSize", [this](float value) { this->pageSize = value; });

    this->recycler->registerCell("Cell", VideoCardCell::create);
}

SearchList::~SearchList() { brls::Logger::debug("View SearchList: delete"); }

void SearchList::doRequest(const std::string& searchTerm) {
    // attribut XML itemType "Movie"/"Series" → searchTypes Plex ; la colonne
    // « Episode » n'a pas d'équivalent /library/search (retirée du XML)
    bool series = this->itemType == "Series";
    std::string query = HTTP::encode_form({
        {"query", searchTerm},
        {"searchTypes", series ? "tv" : "movies"},
        {"includeCollections", "1"},
        {"limit", std::to_string(this->pageSize)},
    });
    std::string wanted = series ? plex::mediaTypeShow : plex::mediaTypeMovie;

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN, wanted](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            // /library/search renvoie des types mêlés → filtre côté client
            std::vector<plex::Item> items;
            for (auto& it : r.Items) {
                if (it.type == wanted) items.push_back(it);
            }
            if (items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
                this->title->setSubtitle(std::to_string(items.size()));
                this->recycler->setDataSource(new VideoDataSource(items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        plex::apiSearch, query);
}

brls::View* SearchList::create() { return new SearchList(); }