/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "view/recycling_grid.hpp"

class MediaFolders : public brls::Box {
public:
    MediaFolders();

    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recyclerFolders, "media/folders");

    void onRequest();
};
