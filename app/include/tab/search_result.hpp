/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class SearchList;

class SearchResult : public brls::Box {
public:
    SearchResult(const std::string& searchTerm);
    ~SearchResult() override;

    static brls::View* create();

private:
    BRLS_BIND(SearchList, searchMovie, "search/movie");
    BRLS_BIND(SearchList, searchSeries, "search/series");
    BRLS_BIND(SearchList, searchEpisode, "search/episode");
};