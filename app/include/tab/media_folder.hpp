/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "view/auto_tab_frame.hpp"

class RecyclingGrid;

class MediaFolders : public AttachedView {
public:
    MediaFolders();
    ~MediaFolders();

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recyclerFolders, "media/folders");

    void doRequest();
};
