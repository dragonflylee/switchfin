/*
    pleNx — connexion à un compte plex.tv (flux PIN, unique méthode).
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
    /// Demande un code à plex.tv et lance l'interrogation périodique
    void startPin();
    void pollOnce();

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

    BRLS_BIND(brls::Label, labelCode, "plex/label/code");
    BRLS_BIND(brls::Label, labelStatus, "plex/label/status");
    BRLS_BIND(brls::Button, btnRetry, "plex/retry");

    brls::RepeatingTimer ticker;
    brls::Time deadline = 0;
    plex::PinResult pin;
};
