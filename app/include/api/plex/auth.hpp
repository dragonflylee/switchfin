/*
    GMCA — plex.tv authentication and server discovery.
    Specification: PLEX_MIGRATION.md §2.2 (PIN flow) and §2.3 (resources + connections).

    All functions are SYNCHRONOUS (they chain HTTP requests): call them from
    brls::async, like the Quick Connect flow used to.
    They throw std::runtime_error on network/HTTP failure.
*/

#pragma once

#include "api/plex/types.hpp"

namespace plex {

/// Creates a 4-character link PIN that the user enters on
/// https://plex.tv/link.
PinResult requestPin();

/// Polls the PIN: returns the account token once the user has validated,
/// empty string until then. Throws if the PIN has expired (404/410).
/// Recommended cadence: 1 s -> 2 s -> 4 s, capped at 5 s, give up at 2 min (§2.2).
std::string pollPin(int64_t pinId);

/// Validates a token (GET /api/v2/user). Throws if invalid/revoked (401/403).
AccountUser getUser(const std::string& accountToken);

/// Servers of the account with their own access tokens (§2.3).
std::vector<ServerResource> getResources(const std::string& accountToken);

/// Plex Home profiles of the account.
std::vector<HomeUser> getHomeUsers(const std::string& accountToken);

/// Switches to a Home profile: returns the token SPECIFIC to that profile.
/// `pin` required when HomeUser::isProtected (error 1041 = wrong PIN).
std::string switchHomeUser(const std::string& accountToken, const std::string& userUuid, const std::string& pin = "");

/// Probes a base URL (GET {base}/ with token); returns true on 200.
bool probeConnection(const std::string& baseUrl, const std::string& accessToken, long timeoutMs = 2000);

/// Candidate base URLs of a server, ordered by priority (https+local ->
/// https+remote -> https+relay -> http...) WITHOUT probing any of them.
/// Used to persist a server's connection list ahead of a lazy probe at switch
/// time; findBestConnection probes this list in order.
std::vector<std::string> rankConnections(const ServerResource& server);

/// Picks the best connection for a server: tries `preferredUri` then the
/// candidates by priority https+local -> https+remote -> https+relay -> http...
/// Returns the reachable base URL, or "".
/// NOTE: sequential probing for now; racing all candidates in parallel is a
/// planned phase 2 optimization.
std::string findBestConnection(const ServerResource& server, const std::string& preferredUri = "");

}  // namespace plex
