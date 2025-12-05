/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>

class RecylingVideo;

class HomeTab : public AttachedView, public Presenter {
public:
    HomeTab();
    ~HomeTab() override;

    void onCreate() override;

    void doRequest() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, boxHome, "home/box");
    BRLS_BIND(RecylingVideo, userResume, "home/user/resume");
    BRLS_BIND(RecylingVideo, showNextup, "home/show/nextup");

    std::vector<RecylingVideo*> latest;
};
