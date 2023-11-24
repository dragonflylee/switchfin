/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>

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
    BRLS_BIND(brls::Header, headerVideo, "home/header/video");
    BRLS_BIND(brls::Header, headerMusic, "home/header/music");
    BRLS_BIND(HRecyclerFrame, userResume, "home/user/resume");
    BRLS_BIND(HRecyclerFrame, showNextup, "home/show/nextup");
    BRLS_BIND(HRecyclerFrame, videoLatest, "home/video/latest");
    BRLS_BIND(HRecyclerFrame, musicLatest, "home/music/latest");

    void doResume();
    void doVideoLatest();
    void doMusicLatest();
    void doNextup();

    size_t pageSize = 6;
    size_t latestSize = 20;
    size_t startResume = 0;
    size_t startNextup = 0;
};
