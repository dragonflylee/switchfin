/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;
class SearchHistory;

class SearchTab : public AttachedView {
public:
    SearchTab();
    ~SearchTab();

    void onCreate() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, leftBox, "tv/search/left");
    BRLS_BIND(brls::Box, searchBox, "tv/search/box");
    BRLS_BIND(brls::Label, inputLabel, "tv/search/input");
    BRLS_BIND(SVGImage, searchSVG, "tv/search/svg");
    BRLS_BIND(brls::Box, actionClear, "tv/search/action/clear");
    BRLS_BIND(brls::Box, actionDelete, "tv/search/action/delete");
    BRLS_BIND(brls::Box, actionSpace, "tv/search/action/space");
    BRLS_BIND(brls::Box, actionSearch, "tv/search/action/search");
    BRLS_BIND(brls::Box, keyboardBox, "tv/search/keyboard");
    BRLS_BIND(brls::Box, rightBox, "tv/search/right");
    BRLS_BIND(brls::Box, historyBox, "tv/history/box");
    BRLS_BIND(brls::Box, historyChips, "tv/search/history");
    BRLS_BIND(brls::Header, suggestHeader, "tv/search/section");
    BRLS_BIND(RecyclingGrid, searchSuggest, "tv/search/suggest");

    void buildKeyboard();
    void buildHistoryChips();
    void launchSearch();
    void doSuggest();
    void doSearch(const std::string& searchTerm);
    void updateInput();

    std::string currentSearch;
    std::unique_ptr<SearchHistory> history;
};
