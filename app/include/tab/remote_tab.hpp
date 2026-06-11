/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

struct AppRemote;

class RemoteTab : public AttachedView {
public:
    RemoteTab();
    ~RemoteTab() override;

    static brls::View* create();

    void onCreate() override;

    /// Rebuilds the pills (remote servers + Downloads + Files)
    /// after a server is added/edited/deleted.
    void refresh();

private:
    /// "Manage server" menu (edit / delete) for a remote server
    void manageRemote(size_t index, const AppRemote& r);

    BRLS_BIND(AutoTabFrame, tabFrame, "remote/tabFrame");
};
