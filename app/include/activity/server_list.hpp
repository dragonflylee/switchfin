/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "utils/config.hpp"

/// Logged-out root: hosts the ConnectionSwitcher grid (+ Settings/Remote footer
/// actions, since there is no app shell yet). When connected, the switcher is
/// instead shown as a detail view over the app (sidebar stays) — see
/// MainActivity's sidebar avatar.
class ServerList : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/server_list.xml");

    ServerList();
    ~ServerList();

    void onContentAvailable() override;

private:
    BRLS_BIND(brls::AppletFrame, frame, "server/frame");
    BRLS_BIND(brls::Box, content, "switcher/content");
};
