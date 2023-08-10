#pragma once

#include "view/recycling_grid.hpp"

class SuggestCard : public RecyclingGridItem {
public:
    SuggestCard() { this->inflateFromXMLRes("xml/view/suggest_card.xml"); }

    static RecyclingGridItem* create() { return new SuggestCard(); }

    BRLS_BIND(brls::Label, content, "suggest/card/content");
};

class SuggestList : public brls::Box {
public:
    SuggestList();
    ~SuggestList() override;

    void setSource(RecyclingGridDataSource *source);
    void setError(const std::string& error);

    static brls::View *create();

private:
    BRLS_BIND(RecyclingGrid, recycling, "search/recycling");
};