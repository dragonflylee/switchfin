/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class HRecyclerFrame;

class HomeTab : public brls::Box {
public:
    HomeTab();

    static brls::View* create();

private:
    BRLS_BIND(HRecyclerFrame, userResume, "user/resume");
    BRLS_BIND(HRecyclerFrame, showNextup, "show/nextup");
    BRLS_BIND(HRecyclerFrame, userLatest, "user/latest");

    void doResume();
    void doLatest();
    void doNextup();
};
