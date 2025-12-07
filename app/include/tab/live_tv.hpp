/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;
class AutoTabFrame;

class LiveTV : public brls::Box {
public:
    LiveTV(const std::string& itemId);

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    void doRequest();
};