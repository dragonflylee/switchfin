/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "view/auto_tab_frame.hpp"

class HRecyclerFrame;

class HomeTab : public AttachedView {
public:
    HomeTab();

    void onCreate() override;

    static brls::View* create();

private:
    BRLS_BIND(HRecyclerFrame, userResume, "user/resume");
    BRLS_BIND(HRecyclerFrame, showNextup, "show/nextup");
    BRLS_BIND(HRecyclerFrame, userLatest, "user/latest");

    void doResume();
    void doLatest();
    void doNextup();
};
