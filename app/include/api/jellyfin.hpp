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

template <typename Then>
void getJSON(
    const std::string& path, Then then, OnError error = [](...) {}) {
    HTTP::Header header;
    const std::string& token = AppConfig::instance().getAccessToken();
    const std::string& url = AppConfig::instance().getServerUrl();
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
            brls::sync(std::bind(std::move(error), std::move(ex)));
        }, url + path, header,
        HTTP::Timeout{1000});
}

template <typename Then>
void postJSON(
    const std::string& path, const nlohmann::json& data, Then then, OnError error = [](...) {}) {
    HTTP::Header header = {"Content-Type: application/json"};
    const std::string& token = AppConfig::instance().getAccessToken();
    const std::string& url = AppConfig::instance().getServerUrl();
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
            brls::sync(std::bind(std::move(error), std::move(ex)));
        }, url + path,
        data.dump(), header, HTTP::Timeout{1000});
}

};  // namespace jellyfin

#include "api/jellyfin/system.hpp"
#include "api/jellyfin/media.hpp"
