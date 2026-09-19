/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <memory>
#endif

class RecylingVideo;

class HomeTab : public AttachedView, public Presenter {
public:
    HomeTab();
    ~HomeTab() override;

    void onCreate() override;

    void doRequest() override;

    static brls::View* create();

private:
#ifdef PS5_NATIVE_GPU
    void loadLibraries();
    struct LibraryLifetime { bool active = true; size_t generation = 0; };
    std::shared_ptr<LibraryLifetime> libraryLifetime = std::make_shared<LibraryLifetime>();
    std::shared_ptr<std::atomic_bool> libraryCancelled;
    std::shared_ptr<std::atomic_bool> libraryAccountCancelled;
    bool actionsRegistered = false;
    bool librariesLoading = false;
    bool librariesLoaded = false;

#endif
    BRLS_BIND(brls::Box, boxHome, "home/box");
    BRLS_BIND(RecylingVideo, userResume, "home/user/resume");
    BRLS_BIND(RecylingVideo, showNextup, "home/show/nextup");

    std::vector<RecylingVideo*> latest;
};
