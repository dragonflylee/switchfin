/*
    pleNx — sign-in form for a Jellyfin or Emby server.
    URL + username/password login. The backend type ("jellyfin" | "emby") only
    changes the title and the value stored in the saved AppServer. On success
    the active server/user are persisted and the app enters MainActivity.

    Reached from ServerTypeChoose (the Jellyfin/Emby cell present()s this view),
    so it is a content view of the ServerList AppletFrame: it keeps the footer
    and B (the frame's hints/back action) returns to the type chooser.
*/

#pragma once

#include <borealis.hpp>

namespace jellyfin {
struct LoginResult;
}

class JellyfinAdd : public brls::Box {
public:
    /// `type` is "jellyfin" or "emby".
    explicit JellyfinAdd(const std::string& type);
    ~JellyfinAdd() override;

    /// Focus the URL field on entry (deterministic, no phantom focus).
    brls::View* getDefaultFocus() override;

private:
    /// Validates the URL, authenticates (async + spinner) then saves.
    void submit();
    /// Persists the active server + user and enters the application.
    void finish(const std::string& url, const jellyfin::LoginResult& r);

    BRLS_BIND(brls::Label, header, "jellyfin/add/header");
    BRLS_BIND(brls::InputCell, cellUrl, "jellyfin/add/url");
    BRLS_BIND(brls::InputCell, cellUser, "jellyfin/add/user");
    BRLS_BIND(brls::InputCell, cellPasswd, "jellyfin/add/passwd");
    BRLS_BIND(brls::Button, btnConnect, "jellyfin/add/connect");
    BRLS_BIND(brls::ProgressSpinner, spinner, "jellyfin/add/spinner");
    BRLS_BIND(brls::Label, labelStatus, "jellyfin/add/status");

    std::string type;
};
