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
inline void getJSON(Then then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"X-Emby-Token: " + c.getUser().access_token};
        const long timeout = c.getItem(AppConfig::REQUEST_TIMEOUT, default_timeout);

        try {
            auto resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{timeout});
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, ex.what()));
        }
    });
}

template <typename Then, typename... Args>
inline void postJSON(const nlohmann::json& data, Then then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url, data]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {
            "Content-Type: application/json",
            "X-Emby-Token: " + c.getUser().access_token,
        };
        const long timeout = c.getItem(AppConfig::REQUEST_TIMEOUT, default_timeout);

        try {
            auto resp = HTTP::post(c.getUrl() + url, data.dump(), header, HTTP::Timeout{timeout});
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
