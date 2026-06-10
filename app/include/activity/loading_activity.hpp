/*
    Écran de chargement plein écran : spinner + « Connexion au serveur… ».
    Affiché pendant la sonde des URL du serveur (plex::probeConnection,
    timeout 2 s par URL, en série — les serveurs plex.direct annoncent
    souvent 10+ connexions dont des IP locales injoignables), au démarrage
    (AppConfig::checkLogin) comme à la sélection d'un profil (ServerList).
*/

#pragma once

#include <borealis.hpp>

class LoadingActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/loading.xml");

    LoadingActivity();

    ~LoadingActivity() override;
};
