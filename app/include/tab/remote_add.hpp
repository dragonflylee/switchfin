/*
    pleNx — formulaire d'ajout/édition d'un serveur de fichiers distant
    (WebDAV / FTP / SFTP / index HTTP). Panneau latéral poussé en activité
    (même patron que PlayerSetting). La sauvegarde est conditionnée à un
    test de connexion réussi : listing du chemin de départ via le client
    correspondant (app/src/client/client.cpp route le schéma d'URL).
*/

#pragma once

#include <borealis.hpp>
#include <utils/config.hpp>

class RemoteAdd : public brls::Box {
public:
    /// editIndex < 0 : ajout ; sinon édition de AppConfig::getRemotes()[editIndex]
    RemoteAdd(std::function<void()> onDone, int editIndex = -1);
    ~RemoteAdd() override;

    /// Pousse le formulaire en activité. `onDone` est appelé après une
    /// sauvegarde réussie (l'appelant rafraîchit sa liste de serveurs).
    static void open(std::function<void()> onDone, int editIndex = -1);

private:
    BRLS_BIND(brls::Header, header, "remote/add/header");
    BRLS_BIND(brls::SelectorCell, cellType, "remote/add/type");
    BRLS_BIND(brls::InputCell, cellName, "remote/add/name");
    BRLS_BIND(brls::InputCell, cellHost, "remote/add/host");
    BRLS_BIND(brls::InputNumericCell, cellPort, "remote/add/port");
    BRLS_BIND(brls::InputCell, cellUser, "remote/add/user");
    BRLS_BIND(brls::InputCell, cellPasswd, "remote/add/passwd");
    BRLS_BIND(brls::InputCell, cellPath, "remote/add/path");
    BRLS_BIND(brls::Button, btnSave, "remote/add/save");
    BRLS_BIND(brls::ProgressSpinner, spinner, "remote/add/spinner");
    BRLS_BIND(brls::Box, cancel, "remote/add/cancel");

    /// Assemble un AppRemote depuis les champs ; lève std::runtime_error
    /// (message i18n) si un champ requis manque.
    AppRemote build();
    /// Valide, teste la connexion (async + spinner) puis enregistre.
    void submit();

    std::function<void()> onDone;
    int editIndex;
    /// index courant du sélecteur de type (pour ne remplacer le port que
    /// s'il vaut encore le défaut du type précédent)
    int typeIndex = 0;
};
