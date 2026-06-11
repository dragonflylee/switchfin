/*
    pleNx — plex.tv authentication implementation.
    Specification: PLEX_MIGRATION.md §2.2-2.3.
*/

#include "api/plex/auth.hpp"
#include "api/plex.hpp"

namespace plex {

/// Runs `fn` against the primary API (clients.plex.tv) then retries on
/// plex.tv in case of transient failure.
template <typename Fn>
static auto withFallback(Fn&& fn) {
    try {
        return fn(tvApiBase);
    } catch (const std::exception& ex) {
        brls::Logger::warning("plex.tv api primaire en échec ({}), repli sur {}", ex.what(), tvApiFallback);
        return fn(tvApiFallback);
    }
}

PinResult requestPin() {
    return withFallback([](const std::string& base) {
        // POST {base}/pins, empty body — weak 4-character PIN for
        // plex.tv/link (cf. tvApiPins comment)
        std::string url = fmt::format(fmt::runtime(tvApiPins), base);
        return postSync(url, "").get<PinResult>();
    });
}

std::string pollPin(int64_t pinId) {
    // 404/410 (expired PIN) surfaces as an "http status 4xx" exception
    return withFallback([pinId](const std::string& base) {
        std::string url = fmt::format(fmt::runtime(tvApiPinPoll), base, pinId);
        return getSync(url, "").get<PinResult>().authToken;
    });
}

AccountUser getUser(const std::string& accountToken) {
    return withFallback([&accountToken](const std::string& base) {
        std::string url = fmt::format(fmt::runtime(tvApiUser), base);
        return getSync(url, accountToken).get<AccountUser>();
    });
}

std::vector<ServerResource> getResources(const std::string& accountToken) {
    return withFallback([&accountToken](const std::string& base) {
        std::string url = fmt::format(fmt::runtime(tvApiResources), base);
        nlohmann::json j = getSync(url, accountToken, 10000);

        // The response is an array of resources; keep only provides=server
        std::vector<ServerResource> servers;
        if (!j.is_array()) return servers;
        for (auto& e : j) {
            if (jstr(e, "provides").find("server") == std::string::npos) continue;
            ServerResource s = e.get<ServerResource>();
            if (!s.clientIdentifier.empty() && !s.connections.empty()) servers.push_back(std::move(s));
        }
        return servers;
    });
}

std::vector<HomeUser> getHomeUsers(const std::string& accountToken) {
    return withFallback([&accountToken](const std::string& base) {
        std::string url = fmt::format(fmt::runtime(tvApiHomeUsers), base);
        nlohmann::json j = getSync(url, accountToken);
        std::vector<HomeUser> users;
        // v2 returns an object containing `users`, some deployments a bare array
        const nlohmann::json& arr = j.contains("users") ? j.at("users") : j;
        if (arr.is_array()) users = arr.get<std::vector<HomeUser>>();
        return users;
    });
}

std::string switchHomeUser(const std::string& accountToken, const std::string& userUuid, const std::string& pin) {
    // POST {base}/home/users/{uuid}/switch — parameters in query, empty body
    // (wrong PIN = 403 with code 1041)
    HTTP::Form form = {
        {"includeSubscriptions", "1"},
        {"includeProviders", "1"},
        {"includeSettings", "1"},
        {"includeSharedSettings", "1"},
        {"X-Plex-Language", "en"},
    };
    if (!pin.empty()) form["pin"] = pin;
    std::string query = HTTP::encode_form(form);

    return withFallback([&](const std::string& base) {
        std::string url = fmt::format(fmt::runtime(tvApiSwitchUser), base, userUuid, query);
        nlohmann::json j = postSync(url, accountToken);
        std::string token = jstr(j, "authToken");
        if (token.empty()) throw std::runtime_error("switchHomeUser: authToken absent de la réponse");
        return token;
    });
}

bool probeConnection(const std::string& baseUrl, const std::string& accessToken, long timeoutMs) {
    // GET {base}/ WITH token: the root requires authentication, which
    // immediately detects a revoked token
    try {
        HTTP::get(baseUrl + "/", headers(accessToken), HTTP::Timeout{timeoutMs});
        return true;
    } catch (const std::exception& ex) {
        brls::Logger::debug("plex probe {} en échec : {}", baseUrl, ex.what());
        return false;
    }
}

std::string findBestConnection(const ServerResource& server, const std::string& preferredUri) {
    // Remembered endpoint first, with a shorter timeout (first phase of the
    // connection race)
    if (!preferredUri.empty() && probeConnection(preferredUri, server.accessToken, 1500)) return preferredUri;

    // Priority: https+local -> https+remote -> https+relay -> http+local -> ... (§2.3)
    auto rank = [](const Connection& c) {
        int r = (c.protocol == "https") ? 0 : 3;
        if (c.relay)
            r += 2;
        else if (!c.local)
            r += 1;
        return r;
    };
    std::vector<Connection> candidates;
    for (auto& c : server.connections) {
        // Unreachable addresses: null or link-local IPv6
        if (c.address == "::" || c.address.rfind("fe80:", 0) == 0) continue;
        candidates.push_back(c);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [&rank](const Connection& a, const Connection& b) { return rank(a) < rank(b); });

    for (auto& c : candidates) {
        std::string base = c.uri.empty() ? fmt::format("{}://{}:{}", c.protocol, c.address, c.port) : c.uri;
        if (base == preferredUri) continue;
        if (probeConnection(base, server.accessToken)) return base;
    }
    return "";
}

}  // namespace plex
