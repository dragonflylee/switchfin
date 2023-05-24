/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "utils/config.hpp"

class RecyclingGrid;

class ServerList : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/server_list.xml");

    ServerList();
    ~ServerList();

    void onContentAvailable() override;
    void onSelect(const AppServer& s);

private:
    BRLS_BIND(brls::AppletFrame, appletFrame, "server/frame");
    BRLS_BIND(RecyclingGrid, recyclerServers, "server/recycler");
    BRLS_BIND(RecyclingGrid, recyclerUsers, "user/recycler");
    BRLS_BIND(brls::DetailCell, serverVersion, "server/version");
    BRLS_BIND(brls::DetailCell, serverOS, "server/os");
    BRLS_BIND(brls::SelectorCell, selectorUrl, "server/urls");
    BRLS_BIND(brls::Button, btnSignin, "server/signin");
};