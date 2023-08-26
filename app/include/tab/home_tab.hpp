/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "view/auto_tab_frame.hpp"
#include "view/presenter.h"

class HRecyclerFrame;

class HomeTab : public AttachedView, public Presenter {
public:
    HomeTab();
    ~HomeTab() override;

    void onCreate() override;

    void doRequest() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Header, headerResume, "home/header/resume");
    BRLS_BIND(brls::Header, headerNextup, "home/header/nextup");
    BRLS_BIND(brls::Header, headerLatest, "home/header/latest");
    BRLS_BIND(HRecyclerFrame, userResume, "home/user/resume");
    BRLS_BIND(HRecyclerFrame, showNextup, "home/show/nextup");
    BRLS_BIND(HRecyclerFrame, userLatest, "home/user/latest");

    void doResume();
    void doLatest();
    void doNextup();

    size_t pageSize = 5;
    size_t latestSize = 20;
    size_t startResume = 0;
    size_t startNextup = 0;
};
