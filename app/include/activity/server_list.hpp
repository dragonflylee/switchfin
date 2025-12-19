/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "utils/config.hpp"

class RecyclingGrid;
class AutoTabFrame;
class ServerCell;

class ServerList : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/server_list.xml");

    ServerList();
    ~ServerList();

    void onContentAvailable() override;
    std::string getUrl();
    void onUser(const std::string& id);
    void willAppear(bool resetState = false) override;

private:
    BRLS_BIND(brls::Button, btnServerAdd, "btn/server/add");
    BRLS_BIND(brls::Box, sidebarServers, "server/sidebar");
    BRLS_BIND(brls::Box, serverDetail, "server/detail");
    BRLS_BIND(RecyclingGrid, recyclerUsers, "user/recycler");
    BRLS_BIND(brls::DetailCell, inputUrl, "selector/server/urls");
    BRLS_BIND(brls::Button, btnSignin, "btn/server/signin");
    BRLS_BIND(AutoTabFrame, tabFrame, "server/tabFrame");

    void onServer(const AppServer &s);
    void setActive(brls::View *active);
    void getActive();
};