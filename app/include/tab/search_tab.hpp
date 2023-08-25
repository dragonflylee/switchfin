/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "view/auto_tab_frame.hpp"

class RecyclingGrid;
class SuggestList;

class SearchTab : public AttachedView {
public:
    SearchTab();
    ~SearchTab();

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recyclingKeyboard, "tv/search/keyboard");
    BRLS_BIND(SuggestList, searchSuggest, "tv/search/suggest");
    BRLS_BIND(SuggestList, searchHistory, "tv/search/history");
    BRLS_BIND(brls::Box, searchBox, "tv/search/box");
    BRLS_BIND(brls::Label, inputLabel, "tv/search/input");
    BRLS_BIND(brls::Label, clearLabel, "tv/search/clear");
    BRLS_BIND(brls::Label, deleteLabel, "tv/search/delete");
    BRLS_BIND(brls::Label, searchLabel, "tv/search/label");
    BRLS_BIND(SVGImage, searchSVG, "tv/search/svg");

    void doSuggest();
    void doSearch(const std::string& searchTerm);
    void updateInput();

    std::string currentSearch;
};
