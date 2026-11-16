/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <vector>

class RemoteView;

class RemoteTab : public AttachedView {
public:
    RemoteTab();
    ~RemoteTab() override;

    static brls::View* create();

    void onCreate() override;

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "remote/tabFrame");

    void refresh();
    /// Reload one server in place instead of rebuilding the tab frame.
    void refreshRemote(size_t index);

    /// RemoteViews indexed like AppConfig::getRemotes().
    std::vector<RemoteView*> views;
};
