/*
    pleNx — Jellyfin/Emby authentication (see jellyfin/auth.hpp).
*/

#include "api/jellyfin/auth.hpp"
#include "api/jellyfin/types.hpp"

namespace jellyfin {

std::string getServerName(const std::string& baseUrl) {
    nlohmann::json j = getSync(baseUrl + std::string(apiPublicInfo), "");
    return jstr(j, "ServerName", "Jellyfin");
}

LoginResult login(const std::string& baseUrl, const std::string& user, const std::string& pass) {
    nlohmann::json body = {{"Username", user}, {"Pw", pass}};
    nlohmann::json j = postSync(baseUrl + std::string(apiAuthByName), "", body.dump());

    LoginResult r;
    r.token = jstr(j, "AccessToken");
    r.serverId = jstr(j, "ServerId");
    if (j.contains("User") && j["User"].is_object()) {
        r.userId = jstr(j["User"], "Id");
        r.userName = jstr(j["User"], "Name");
    }
    if (r.token.empty() || r.userId.empty()) throw std::runtime_error("Authentication failed");
    return r;
}

}  // namespace jellyfin
