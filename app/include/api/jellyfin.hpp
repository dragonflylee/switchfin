/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "api/http.hpp"
#include "utils/config.hpp"
#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>

namespace jellyfin {

using OnError = std::function<void(const std::string&)>;

std::string defaultAuthHeader();

template <typename Then, typename... Args>
inline void getJSON(Then then, OnError error, const std::string& fmt, Args&&... args) {
    HTTP::Header header;
    const std::string& token = AppConfig::instance().getAccessToken();
    std::string url = fmt::format(fmt::runtime(fmt), AppConfig::instance().getServerUrl(), std::forward<Args>(args)...);
    if (token.empty())
        header.push_back(defaultAuthHeader());
    else
        header.push_back("X-Emby-Token: " + token);
    HTTP::get_async(
        [then](const std::string& resp) {
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        },
        [error](const std::string& ex) {
            if (error) brls::sync(std::bind(std::move(error), std::move(ex)));
        },
        url, header, HTTP::Timeout{1000});
}

template <typename Then, typename... Args>
inline void postJSON(const nlohmann::json& data, Then then, OnError error, const std::string& fmt, Args&&... args) {
    HTTP::Header header = {"Content-Type: application/json"};
    const std::string& token = AppConfig::instance().getAccessToken();
    std::string url = fmt::format(fmt::runtime(fmt), AppConfig::instance().getServerUrl(), std::forward<Args>(args)...);
    if (token.empty())
        header.push_back(defaultAuthHeader());
    else
        header.push_back("X-Emby-Token: " + token);
    HTTP::post_async(
        [then](const std::string& resp) {
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        },
        [error](const std::string& ex) {
            if (error) brls::sync(std::bind(std::move(error), std::move(ex)));
        },
        url, data.dump(), header, HTTP::Timeout{1000});
}

};  // namespace jellyfin

#include "api/jellyfin/system.hpp"
#include "api/jellyfin/media.hpp"
#include "api/jellyfin/playback.hpp"
