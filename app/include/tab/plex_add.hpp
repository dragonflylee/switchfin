/*
    GMCA — sign in to a Plex server.
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

    /// Steps after the account token is obtained. A single link registers ALL
    /// the account's servers as connections; the profile (if the account has
    /// several Plex Home profiles) is chosen once and applies to every server.
    void onAccount(const std::string& accountToken);
    void onProfilePicked(const plex::HomeUser& home, const std::string& accountToken);
    void doSwitch(const plex::HomeUser& home, const std::string& accountToken, const std::string& pin);

    /// Registers every server of the account, activates one (the first owned,
    /// else the first), and enters the application. Only the activated server is
    /// probed now; the others resolve their reachable url lazily on switch.
    void finishAll(const std::string& uuid, const std::string& name, const std::string& thumb,
        const std::string& plexTvToken, const std::vector<plex::ServerResource>& servers);

    BRLS_BIND(brls::Label, labelCode, "plex/label/code");
    BRLS_BIND(brls::Label, labelStatus, "plex/label/status");
    BRLS_BIND(brls::Button, btnRetry, "plex/retry");

    brls::RepeatingTimer ticker;
    brls::Time deadline = 0;
    plex::PinResult pin;
};
