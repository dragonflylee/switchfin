#include "view/suggest_list.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"

SuggestList::SuggestList() {
    this->inflateFromXMLRes("xml/view/suggest_list.xml");
    brls::Logger::debug("SuggestList: create");

    this->registerFloatXMLAttribute("spanCount", [this](float value) { this->recycling->spanCount = (int)value; });

    this->recycling->registerCell("Card", &SuggestCard::create);
    this->recycling->registerCell("Cell", &VideoCardCell::create);
}
SuggestList::~SuggestList() { brls::Logger::debug("SuggestList: deleted"); }

void SuggestList::setSource(RecyclingGridDataSource* source) {
    this->recycling->setDataSource(source);
}

void SuggestList::setError(const std::string& error) { this->recycling->setError(error); }

brls::View* SuggestList::create() { return new SuggestList(); }
