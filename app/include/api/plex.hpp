/*
    GMCA — HTTP transport to Plex (plex.tv and Plex Media Server).
    Specification: PLEX_MIGRATION.md §2.1 (X-Plex-* headers) and §2.4 (MediaContainer).

    Same philosophy as the former Jellyfin API layer: async helpers that run
    the request in brls::async then sync the parsed result back to the UI
    thread via brls::sync.
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

/// Language to request from the server (localized hub titles): primary
/// subtag of the app locale ("fr-FR" -> "fr").
inline std::string language() {
    std::string locale = brls::Application::getLocale();
    auto pos = locale.find('-');
    return pos == std::string::npos ? locale : locale.substr(0, pos);
}

/// Common X-Plex-* headers. Empty `token` = anonymous request (PIN creation).
/// The caller picks the server or account token.
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

/// Appends the token as a query param — for URLs consumed outside the HTTP
/// client (mpv, images, downloads).
inline std::string withToken(const std::string& url, const std::string& token) {
    if (token.empty()) return url;
    return url + (url.find('?') == std::string::npos ? "?" : "&") + "X-Plex-Token=" + token;
}

/// Synchronous GET returning the parsed JSON. Call from an async context.
inline nlohmann::json getSync(const std::string& url, const std::string& token, long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::get(url, headers(token), HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// Synchronous POST (empty body by default — the plex.tv API uses query params).
inline nlohmann::json postSync(
    const std::string& url, const std::string& token, const std::string& body = "", long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::post(url, body, headers(token), HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// Async GET: parses the response into `Result` (the models from
/// api/plex/types.hpp, including Container<T>) then calls `then` on the UI thread.
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

/// Fire-and-forget async GET (scrobble, timeline...): success is silent,
/// failure is logged or reported.
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

/// Fire-and-forget async PUT (watchlist actions on the plex.tv provider:
/// empty body, parameters in the query string). Success is called back on the UI thread.
template <typename... Args>
inline void putAction(const std::string& base, const std::string& token, const std::function<void()>& then,
    OnError error, std::string_view path, Args&&... args) {
    std::string url = base + fmt::format(fmt::runtime(path), std::forward<Args>(args)...);
    brls::async([then, error, url, token]() {
        try {
            HTTP::put(url, "", headers(token), HTTP::Timeout{});
            if (then) brls::sync(then);
        } catch (const std::exception& ex) {
            if (error)
                brls::sync(std::bind(error, std::string(ex.what())));
            else
                brls::Logger::warning("plex::putAction {}: {}", url, ex.what());
        }
    });
}

/// MediaContainer pagination parameters (query params, despite the X-Plex-* name).
inline void addPagination(HTTP::Form& form, size_t start, size_t size) {
    form["X-Plex-Container-Start"] = std::to_string(start);
    form["X-Plex-Container-Size"] = std::to_string(size);
}

}  // namespace plex
