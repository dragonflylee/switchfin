/*
    Switchlex — authentification plex.tv et découverte des serveurs.
    Spécification : PLEX_MIGRATION.md §2.2 (flux PIN) et §2.3 (resources + connexions).

    Toutes les fonctions sont SYNCHRONES (elles enchaînent des requêtes HTTP) :
    les appeler depuis brls::async, comme le faisait le flux Quick Connect.
    Elles lèvent std::runtime_error en cas d'échec réseau/HTTP.
*/

#pragma once

#include "api/plex/types.hpp"

namespace plex {

/// Crée un PIN d'association à 4 caractères que l'utilisateur saisit sur
/// https://plex.tv/link.
PinResult requestPin();

/// Interroge le PIN : retourne le token de compte dès que l'utilisateur a validé,
/// chaîne vide tant que ce n'est pas le cas. Lève si le PIN a expiré (404/410).
/// Cadence recommandée : 1 s → 2 s → 4 s, plafond 5 s, abandon à 2 min (§2.2).
std::string pollPin(int64_t pinId);

/// Vérifie un token (GET /api/v2/user). Lève si invalide/révoqué (401/403).
AccountUser getUser(const std::string& accountToken);

/// Serveurs du compte avec leurs tokens d'accès propres (§2.3).
std::vector<ServerResource> getResources(const std::string& accountToken);

/// Profils Plex Home du compte.
std::vector<HomeUser> getHomeUsers(const std::string& accountToken);

/// Bascule vers un profil Home : retourne le token PROPRE à ce profil.
/// `pin` requis quand HomeUser::isProtected (erreur 1041 = mauvais PIN).
std::string switchHomeUser(const std::string& accountToken, const std::string& userUuid, const std::string& pin = "");

/// Teste une URL de base (GET {base}/ avec token) ; retourne true si 200.
bool probeConnection(const std::string& baseUrl, const std::string& accessToken, long timeoutMs = 2000);

/// Choisit la meilleure connexion d'un serveur : essaie `preferredUri` puis les
/// candidates par priorité https+local → https+remote → https+relay → http…
/// (plex_auth_service.dart:580-643). Retourne l'URL de base joignable, ou "".
/// NOTE : probing séquentiel pour l'instant ; la course parallèle de plezy
/// (endpoint_race.dart) est une optimisation prévue en phase 2.
std::string findBestConnection(const ServerResource& server, const std::string& preferredUri = "");

}  // namespace plex
