/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RemoteTab : public AttachedView {
public:
    RemoteTab();
    ~RemoteTab() override;

    static brls::View* create();

    void onCreate() override;

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "remote/tabFrame");
};