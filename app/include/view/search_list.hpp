/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class HRecyclerFrame;

class SearchList : public brls::Box {
public:
    SearchList();
    ~SearchList() override;

    void doRequest(const std::string& searchTerm);

    static brls::View* create();

private:
    BRLS_BIND(brls::Header, title, "search/title");
    BRLS_BIND(HRecyclerFrame, recycler, "search/result");
    
    std::string itemType;
    size_t pageSize;
};