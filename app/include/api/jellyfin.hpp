/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "api/http.hpp"
#include "utils/config.hpp"
#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>

namespace jellyfin {

extern std::string defaultAuthHeader;

template <typename Then>
void get(const std::string& path, Then then, HTTP::OnError error = nullptr) {
    HTTP::Header header;
    std::string token = AppConfig::instance().getAccessToken();
    std::string url = AppConfig::instance().getServerUrl();
    if (token.empty())
        header.push_back(defaultAuthHeader);
    else
        header.push_back("X-Emby-Token: " + token);
    HTTP::get_async([then](const std::string& resp) { then(nlohmann::json::parse(resp)); }, error, url + path, header,
        HTTP::Timeout{1000});
}

template <typename Then>
void post(const std::string& path, const nlohmann::json& data, Then then, HTTP::OnError error = nullptr) {
    HTTP::Header header = {"Content-Type: application/json"};
    std::string token = AppConfig::instance().getAccessToken();
    std::string url = AppConfig::instance().getServerUrl();
    if (token.empty())
        header.push_back(defaultAuthHeader);
    else
        header.push_back("X-Emby-Token: " + token);
    HTTP::post_async([then](const std::string& resp) { then(nlohmann::json::parse(resp)); }, error, url + path,
        data.dump(), header, HTTP::Timeout{1000});
}

};  // namespace jellyfin

#include "api/jellyfin/system.hpp"
