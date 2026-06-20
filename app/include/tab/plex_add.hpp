/*
    pleNx — sign in to a Plex server.
    plex.tv PIN flow then server/profile discovery (PLEX_MIGRATION.md §2.2-2.3).
    Reached from ServerTypeChoose once the user picks Plex; it is a content view
    of the ServerList AppletFrame (pushed via View::present), so it inherits the
    footer and B (Back) returns to the type chooser.
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

class PlexAdd : public brls::Box {
public:
    PlexAdd();
    ~PlexAdd() override;

    brls::View* getDefaultFocus() override;

private:
    /// Requests a code from plex.tv and starts the periodic polling
    void startPin();
    void pollOnce();

    /// Steps after the account token is obtained
    void onAccount(const std::string& accountToken);
    void onServerPicked(
        const plex::AccountUser& account, const std::string& accountToken, const plex::ServerResource& server);
    void onProfilePicked(const plex::HomeUser& home, const std::string& accountToken,
        const plex::ServerResource& server, const std::string& baseUrl);
    void doSwitch(const plex::HomeUser& home, const std::string& accountToken, const plex::ServerResource& server,
        const std::string& baseUrl, const std::string& pin);

    /// Saves the active server + user and enters the application
    void finish(const std::string& uuid, const std::string& name, const std::string& thumb,
        const std::string& plexTvToken, const plex::ServerResource& server, const std::string& baseUrl);

    BRLS_BIND(brls::Label, labelCode, "plex/label/code");
    BRLS_BIND(brls::Label, labelStatus, "plex/label/status");
    BRLS_BIND(brls::Button, btnRetry, "plex/retry");

    brls::RepeatingTimer ticker;
    brls::Time deadline = 0;
    plex::PinResult pin;
};
