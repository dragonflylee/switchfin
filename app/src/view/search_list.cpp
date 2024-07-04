#include "view/search_list.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "api/jellyfin.hpp"

SearchList::SearchList() {
    brls::Logger::debug("View SearchList: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/view/search_list.xml");

    this->registerStringXMLAttribute("title", [this](std::string value) { this->searchTitle->setTitle(value); });

    this->registerStringXMLAttribute("itemType", [this](std::string value) { this->itemType = value; });

    this->registerFloatXMLAttribute("pageSize", [this](float value) { this->pageSize = value; });

    this->searchResult->registerCell("Cell", VideoCardCell::create);
}

SearchList::~SearchList() { brls::Logger::debug("View SearchList: delete"); }

void SearchList::doRequest(const std::string& searchTerm) {
    std::string query = HTTP::encode_form({
        {"searchTerm", searchTerm},
        {"IncludeItemTypes", this->itemType},
        {"Recursive", "true"},
        {"IncludeMedia", "true"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"limit", std::to_string(this->pageSize)},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
            } else {
                this->searchTitle->setSubtitle(fmt::format("{}", r.TotalRecordCount));
                this->searchResult->setDataSource(new VideoDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}

brls::View* SearchList::create() { return new SearchList(); }