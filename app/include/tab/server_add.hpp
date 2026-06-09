/*
    Switchlex — connexion à un compte plex.tv (flux PIN) ou à un serveur en direct.
    Spécification : PLEX_MIGRATION.md §2.2-2.3.
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

class ServerAdd : public brls::Box {
public:
    ServerAdd();
    ~ServerAdd() override;

    brls::View* getDefaultFocus() override;

private:
    /// Flux PIN plex.tv (équivalent UX du Quick Connect Jellyfin)
    bool onLink();
    /// Saisie manuelle URL + token
    bool onManual();

    /// Étapes après obtention du token de compte
    void onAccount(const std::string& accountToken);
    void onServerPicked(
        const plex::AccountUser& account, const std::string& accountToken, const plex::ServerResource& server);
    void onProfilePicked(const plex::HomeUser& home, const std::string& accountToken,
        const plex::ServerResource& server, const std::string& baseUrl);
    void doSwitch(const plex::HomeUser& home, const std::string& accountToken, const plex::ServerResource& server,
        const std::string& baseUrl, const std::string& pin);

    /// Enregistre serveur + utilisateur actifs et entre dans l'application
    void finish(const std::string& uuid, const std::string& name, const std::string& thumb,
        const std::string& plexTvToken, const plex::ServerResource& server, const std::string& baseUrl);

    BRLS_BIND(brls::DetailCell, btnLink, "plex/link");
    BRLS_BIND(brls::InputCell, inputUrl, "server/url");
    BRLS_BIND(brls::InputCell, inputToken, "server/token");
    BRLS_BIND(brls::DetailCell, btnConnect, "server/connect");
};
