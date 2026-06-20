#include "view/search_list.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"

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
    // itemType XML attribute "Movie"/"Series" -> Plex searchTypes; the
    // "Episode" column has no /library/search equivalent (removed from the XML)
    bool series = this->itemType == "Series";
    std::string wanted = series ? media::mediaTypeShow : media::mediaTypeMovie;

    ASYNC_RETAIN
    AppConfig::instance().backend().search(
        searchTerm, series ? media::MediaKind::Show : media::MediaKind::Movie, (int)this->pageSize,
        [ASYNC_TOKEN, wanted](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            // /library/search returns mixed types -> client-side filter
            std::vector<plex::Item> items;
            for (auto& it : r.Items) {
                if (it.type == wanted) items.push_back(it);
            }
            if (items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
                this->recycler->setDataSource(new VideoDataSource(items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        });
}

brls::View* SearchList::create() { return new SearchList(); }