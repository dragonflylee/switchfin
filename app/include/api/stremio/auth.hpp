/*
    GMCA — Stremio account authentication (api.strem.io login + addon sync).
    Synchronous helper (call from brls::async); throws std::runtime_error on
    failure, carrying the api.strem.io error message when present.

    The Stremio account is NOT a media server: it only holds the addon
    collection. login() returns the account identity (authKey acts as the
    session token) plus the transport URLs of the installed addons, which the
    Stremio backend uses to discover catalogs/streams. Local addons
    (127.0.0.1 / localhost) are filtered out: they require a streaming-server
    daemon that does not exist on console.
*/

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace stremio {

struct Account {
    std::string authKey;  // base64 session token (~44 chars), used as access_token
    std::string userId;   // result.user._id
    std::string userName;// result.user.fullname, else email
    std::vector<std::string> addons;  // transportUrl (…/manifest.json) of remote addons
};

/// POST /api/login then /api/addonCollectionGet. Throws std::runtime_error on
/// HTTP/parse failure or on an `{"error":{…}}` envelope (message preserved).
Account login(const std::string& email, const std::string& password);

// ---- account datastore: the user's "library" (= watchlist) + playback state ----
// All synchronous (call from brls::async); throw std::runtime_error on failure.

/// Current UTC time, ISO-8601 with milliseconds ("2026-06-16T16:57:00.123Z").
/// Used for LibraryItem _ctime/_mtime/lastWatched.
std::string nowIso();

/// POST /api/datastoreGet for the "libraryItem" collection → the raw items array.
nlohmann::json datastoreGet(const std::string& authKey);

/// POST /api/datastorePut for the "libraryItem" collection. `items` may be a
/// single LibraryItem object or an array of them.
void datastorePut(const std::string& authKey, const nlohmann::json& items);

}  // namespace stremio
