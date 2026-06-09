#include "tab/search_result.hpp"
#include "view/search_list.hpp"

SearchResult::SearchResult(const std::string& searchTerm) {
    brls::Logger::debug("Tab SearchResult: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/search_result.xml");

    this->searchMovie->doRequest(searchTerm);
    this->searchSeries->doRequest(searchTerm);
}

SearchResult::~SearchResult() { brls::Logger::debug("Tab SearchResult: delete"); }