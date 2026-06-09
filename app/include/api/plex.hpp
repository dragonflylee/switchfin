/*
    Switchlex — transport HTTP vers Plex (plex.tv et Plex Media Server).
    Spécification : PLEX_MIGRATION.md §2.1 (en-têtes X-Plex-*) et §2.4 (MediaContainer).

    Même philosophie que l'ancienne couche API Jellyfin : helpers asynchrones qui
    exécutent la requête dans brls::async puis resynchronisent le résultat
    parsé sur le thread UI via brls::sync.
*/

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/application.hpp>
#include "api/http.hpp"
#include "api/plex/types.hpp"
#include "utils/config.hpp"

namespace plex {

using OnError = std::function<void(const std::string&)>;

/// Langue à demander au serveur (titres de hubs localisés) : sous-tag
/// primaire de la locale de l'app (« fr-FR » → « fr »).
inline std::string language() {
    std::string locale = brls::Application::getLocale();
    auto pos = locale.find('-');
    return pos == std::string::npos ? locale : locale.substr(0, pos);
}

/// En-têtes X-Plex-* communs (plex_config.dart:50-67). `token` vide = requête
/// anonyme (création de PIN). Le token serveur ou compte est choisi par l'appelant.
inline HTTP::Header headers(const std::string& token = "") {
    HTTP::Header h = {
        "Accept: application/json",
        "Accept-Charset: utf-8",
        fmt::format("X-Plex-Product: {}", AppVersion::getPackageName()),
        fmt::format("X-Plex-Version: {}", AppVersion::getVersion()),
        fmt::format("X-Plex-Platform: {}", AppVersion::getPlatform()),
        fmt::format("X-Plex-Device-Name: {}", AppVersion::getDeviceName()),
        fmt::format("X-Plex-Client-Identifier: {}", AppConfig::instance().getDeviceId()),
        fmt::format("X-Plex-Language: {}", language()),
        "X-Plex-Client-Profile-Name: Generic",
    };
    if (!token.empty()) h.push_back(fmt::format("X-Plex-Token: {}", token));
    return h;
}

/// Ajoute le token en query param — pour les URLs consommées hors client HTTP
/// (mpv, images, téléchargements) (plex_url_helper.dart:13-21).
inline std::string withToken(const std::string& url, const std::string& token) {
    if (token.empty()) return url;
    return url + (url.find('?') == std::string::npos ? "?" : "&") + "X-Plex-Token=" + token;
}

/// GET synchrone retournant le JSON parsé. À appeler depuis un contexte async.
inline nlohmann::json getSync(const std::string& url, const std::string& token, long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::get(url, headers(token), HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// POST synchrone (corps vide par défaut — l'API plex.tv utilise les query params).
inline nlohmann::json postSync(
    const std::string& url, const std::string& token, const std::string& body = "", long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::post(url, body, headers(token), HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// GET asynchrone : parse la réponse vers `Result` (les modèles de
/// api/plex/types.hpp, dont Container<T>) puis rappelle `then` sur le thread UI.
template <typename Result, typename... Args>
inline void getJSON(const std::string& base, const std::string& token, const std::function<void(Result)>& then,
    OnError error, std::string_view path, Args&&... args) {
    std::string url = base + fmt::format(fmt::runtime(path), std::forward<Args>(args)...);
    brls::async([then, error, url, token]() {
        try {
            auto j = getSync(url, token).get<Result>();
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/// GET asynchrone « fire and forget » (scrobble, timeline…) : le succès est
/// silencieux, l'échec est journalisé ou remonté.
template <typename... Args>
inline void getAction(
    const std::string& base, const std::string& token, OnError error, std::string_view path, Args&&... args) {
    std::string url = base + fmt::format(fmt::runtime(path), std::forward<Args>(args)...);
    brls::async([error, url, token]() {
        try {
            HTTP::get(url, headers(token), HTTP::Timeout{});
        } catch (const std::exception& ex) {
            if (error)
                brls::sync(std::bind(error, std::string(ex.what())));
            else
                brls::Logger::warning("plex::getAction {}: {}", url, ex.what());
        }
    });
}

/// Paramètres de pagination MediaContainer (query params, malgré le nom X-Plex-*)
/// (plex_client.dart:928-933).
inline void addPagination(HTTP::Form& form, size_t start, size_t size) {
    form["X-Plex-Container-Start"] = std::to_string(start);
    form["X-Plex-Container-Size"] = std::to_string(size);
}

}  // namespace plex
