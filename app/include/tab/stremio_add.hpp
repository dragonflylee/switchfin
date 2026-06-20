/*
    pleNx — sign-in form for a Stremio account (api.strem.io).
    Email + password login. On success the account's authKey and synced addon
    collection are persisted as an AppServer (type "stremio") + AppUser and the
    app enters MainActivity.

    Reached from ServerTypeChoose (the Stremio cell present()s this view), so it
    is a content view of the ServerList AppletFrame: it keeps the footer and B
    (the frame's hints/back action) returns to the type chooser.
*/

#pragma once

#include <borealis.hpp>

namespace stremio {
struct Account;
}

class StremioAdd : public brls::Box {
public:
    StremioAdd();
    ~StremioAdd() override;

    /// Focus the email field on entry (deterministic, no phantom focus).
    brls::View* getDefaultFocus() override;

private:
    /// Validates the form, authenticates (async + spinner) then saves.
    void submit();
    /// Persists the active server + user and enters the application.
    void finish(const stremio::Account& a);

    BRLS_BIND(brls::Label, header, "stremio/add/header");
    BRLS_BIND(brls::InputCell, cellEmail, "stremio/add/email");
    BRLS_BIND(brls::InputCell, cellPasswd, "stremio/add/passwd");
    BRLS_BIND(brls::Button, btnConnect, "stremio/add/connect");
    BRLS_BIND(brls::ProgressSpinner, spinner, "stremio/add/spinner");
    BRLS_BIND(brls::Label, labelStatus, "stremio/add/status");
};
