/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include "api/http.hpp"
#include "utils/config.hpp"
#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

namespace jellyfin {

using OnError = std::function<void(const std::string&)>;

template <typename Then, typename... Args>
inline void getJSON(Then then, OnError error, const std::string& fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url]() {
        auto& c = AppConfig::instance();
        HTTP::Header header;
        const std::string& token = c.getUser().access_token;
        if (!token.empty())
            header.push_back("X-Emby-Token: " + token);
        else
            header.push_back(fmt::format(
                "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
                AppVersion::getPlatform(), AppVersion::getDeviceName(), c.getDevice(), AppVersion::getVersion()));

        try {
            std::string resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{1000});
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, ex.what()));
        }
    });
}

template <typename Then, typename... Args>
inline void postJSON(const nlohmann::json& data, Then then, OnError error, const std::string& fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url, data]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"Content-Type: application/json"};
        const std::string& token = c.getUser().access_token;
        if (!token.empty())
            header.push_back("X-Emby-Token: " + token);
        else
            header.push_back(fmt::format(
                "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
                AppVersion::getPlatform(), AppVersion::getDeviceName(), c.getDevice(), AppVersion::getVersion()));
        try {
            std::string resp = HTTP::post(c.getUrl() + url, data.dump(), header, HTTP::Timeout{1000});
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, ex.what()));
        }
    });
}

};  // namespace jellyfin

#include "api/jellyfin/system.hpp"
#include "api/jellyfin/media.hpp"
