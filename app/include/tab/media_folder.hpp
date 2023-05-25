/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;
class AutoTabFrame;

class MediaFolders : public brls::Box {
public:
    MediaFolders();
    ~MediaFolders();

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recyclerFolders, "media/folders");

    AutoTabFrame *getTabFrame();

    void doRequest();
};
