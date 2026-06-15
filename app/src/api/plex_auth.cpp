/*
    pleNx — plex.tv authentication implementation.
    Specification: PLEX_MIGRATION.md §2.2-2.3.
*/

#include "api/plex/auth.hpp"
#include "api/plex.hpp"
#include <cctype>
#include <cstdio>

namespace plex {

/// True when `address` is an RFC1918 / CGNAT / loopback / IPv6-ULA host — i.e.
/// an IP whose plex.direct alias resolves to a non-public address, which routers
/// with DNS-rebinding protection refuse to resolve. We probe such addresses
/// directly (raw IP, bypassing plex.direct) so a LAN/VPN server stays reachable
/// (GH #3). Public addresses gain nothing — plex.direct resolves to the same IP.
/// IPv6 link-local (fe80::) and unspecified (::) are filtered by the caller.
static bool isPrivateAddress(const std::string& address) {
    unsigned a, b, c, d;
    char tail;
    if (std::sscanf(address.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && a <= 255 && b <= 255 &&
        c <= 255 && d <= 255) {
        if (a == 0 || a == 10 || a == 127) return true;     // this-host, RFC1918 /8, loopback
        if (a == 100 && b >= 64 && b <= 127) return true;   // CGNAT 100.64/10 (Tailscale)
        if (a == 169 && b == 254) return true;              // link-local 169.254/16
        if (a == 172 && b >= 16 && b <= 31) return true;    // RFC1918 172.16/12
        if (a == 192 && b == 168) return true;              // RFC1918 192.168/16
        return false;
    }
    if (address.find(':') != std::string::npos) {  // IPv6
        if (address == "::1") return true;          // loopback
        char p0 = (char)std::tolower((unsigned char)address[0]);
        char p1 = address.size() > 1 ? (char)std::tolower((unsigned char)address[1]) : '\0';
        if (p0 == 'f' && (p1 == 'c' || p1 == 'd')) return true;  // ULA fc00::/7 (incl. Tailscale fd7a::)
    }
    return false;
}

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

    // Priority by protocol + reachability: https+local -> https+remote ->
    // https+relay -> http+local -> ... (§2.3)
    auto rank = [](const std::string& protocol, bool local, bool relay) {
        int r = (protocol == "https") ? 0 : 3;
        if (relay)
            r += 2;
        else if (!local)
            r += 1;
        return r;
    };

    struct Candidate {
        std::string url;
        int rank;
    };
    std::vector<Candidate> candidates;
    auto add = [&](const std::string& url, const std::string& protocol, bool local, bool relay) {
        if (url.empty() || url == preferredUri) return;
        for (auto& e : candidates)
            if (e.url == url) return;  // dedupe
        candidates.push_back({url, rank(protocol, local, relay)});
    };

    for (auto& c : server.connections) {
        // Unreachable addresses: null or link-local IPv6
        if (c.address == "::" || c.address.rfind("fe80:", 0) == 0) continue;

        // Advertised URI: a plex.direct hostname for local/remote https servers.
        add(c.uri, c.protocol, c.local, c.relay);

        // For private/CGNAT/ULA addresses, also probe the raw address:port: the
        // plex.direct alias of a non-public IP is refused by DNS-rebinding
        // protection, so the LAN/VPN server looks "unreachable" even though it
        // answers directly (GH #3). The official clients hit the raw LAN IP too.
        // TLS verification is disabled (http.cpp) so the plex.direct cert is
        // accepted on the bare IP. Public addresses (remote/relay) are skipped:
        // their plex.direct alias resolves to the same IP, so a raw probe only
        // lengthens the sequential candidate list.
        if (isPrivateAddress(c.address)) {
            bool ipv6 = c.address.find(':') != std::string::npos;
            std::string host = ipv6 ? fmt::format("[{}]", c.address) : c.address;
            add(fmt::format("{}://{}:{}", c.protocol, host, c.port), c.protocol, c.local, c.relay);
            // Plain-HTTP fallback for servers that only serve HTTP on the LAN IP
            // (Secure connections != "Required").
            if (c.protocol == "https") add(fmt::format("http://{}:{}", host, c.port), "http", c.local, c.relay);
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.rank < b.rank; });

    for (auto& c : candidates) {
        if (probeConnection(c.url, server.accessToken)) return c.url;
    }
    return "";
}

}  // namespace plex
