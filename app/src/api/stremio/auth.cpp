/*
    GMCA — Stremio account authentication (see stremio/auth.hpp).

    api.strem.io envelope: success is {"result":{…}}, failure is
    {"error":{"code":N,"message":"…"}} (no `result`). Verified empirically
    (curl, 2026-06-16): /api/login → result.authKey (~44 char base64) +
    result.user{_id,email,fullname}; /api/addonCollectionGet → result.addons[]
    of descriptors whose transportUrl ends with /manifest.json. A fresh account
    ships 7 default addons, one of which is the local addon
    (http://127.0.0.1:11470/…) — filtered out here.
*/

#include "api/stremio/auth.hpp"
#include "api/stremio/types.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>

namespace stremio {

namespace {

// Generous timeout: login + addon sync are two sequential round-trips to
// api.strem.io, well over the 3s default used for local-server GETs.
constexpr long kAuthTimeout = 15000L;

/// POST JSON to api.strem.io, parse the {result|error} envelope. Throws
/// std::runtime_error with the api.strem.io message on an `error` envelope,
/// or a generic message on HTTP/parse failure. Returns the `result` node.
nlohmann::json postEnvelope(const std::string& url, const nlohmann::json& body) {
    std::string resp =
        HTTP::post(url, body.dump(), HTTP::Header{"Content-Type: application/json"}, HTTP::Timeout{kAuthTimeout});
    if (resp.empty()) throw std::runtime_error("Stremio: empty response");

    nlohmann::json j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw std::runtime_error("Stremio: invalid response");

    if (j.contains("error") && !j["error"].is_null()) {
        std::string msg = jstr(j["error"], "message");
        throw std::runtime_error(msg.empty() ? "Stremio: authentication failed" : msg);
    }
    // result is an object for login/addonCollection/datastorePut, an array for
    // datastoreGet — accept either, reject only absent/null.
    if (!j.contains("result") || j["result"].is_null())
        throw std::runtime_error("Stremio: unexpected response");
    return j["result"];
}

/// A local addon needs the streaming-server daemon absent on console.
bool isLocalAddon(const std::string& transportUrl) {
    return transportUrl.find("127.0.0.1") != std::string::npos ||
           transportUrl.find("localhost") != std::string::npos;
}

}  // namespace

Account login(const std::string& email, const std::string& password) {
    static const std::string base = "https://api.strem.io";

    // 1. Login -> authKey + user identity.
    nlohmann::json loginBody = {{"email", email}, {"password", password}, {"facebook", false}};
    nlohmann::json r = postEnvelope(base + "/api/login", loginBody);

    Account a;
    a.authKey = jstr(r, "authKey");
    if (a.authKey.empty()) throw std::runtime_error("Stremio: authentication failed");
    if (r.contains("user") && r["user"].is_object()) {
        a.userId = jstr(r["user"], "_id");
        a.userName = jstr(r["user"], "fullname");
        if (a.userName.empty()) a.userName = jstr(r["user"], "email");
    }
    if (a.userId.empty()) a.userId = email;

    // 2. Addon collection -> remote transport URLs (skip local addons).
    nlohmann::json collBody = {{"authKey", a.authKey}, {"update", true}};
    nlohmann::json coll = postEnvelope(base + "/api/addonCollectionGet", collBody);
    if (coll.contains("addons") && coll["addons"].is_array()) {
        for (auto& d : coll["addons"]) {
            std::string transportUrl = jstr(d, "transportUrl");
            if (transportUrl.empty() || isLocalAddon(transportUrl)) continue;
            a.addons.push_back(transportUrl);
        }
    }

    return a;
}

std::string nowIso() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count());
    return std::string(buf);
}

nlohmann::json datastoreGet(const std::string& authKey) {
    nlohmann::json body = {{"authKey", authKey}, {"collection", "libraryItem"}, {"all", true}};
    nlohmann::json result = postEnvelope("https://api.strem.io/api/datastoreGet", body);
    return result.is_array() ? result : nlohmann::json::array();
}

void datastorePut(const std::string& authKey, const nlohmann::json& items) {
    nlohmann::json changes = items.is_array() ? items : nlohmann::json::array({items});
    nlohmann::json body = {{"authKey", authKey}, {"collection", "libraryItem"}, {"changes", changes}};
    postEnvelope("https://api.strem.io/api/datastorePut", body);  // throws on error
}

}  // namespace stremio
