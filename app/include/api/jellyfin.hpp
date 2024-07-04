/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include "http.hpp"
#include "utils/config.hpp"

namespace jellyfin {

using OnError = std::function<void(const std::string&)>;

template <typename Result, typename... Args>
inline void getJSON(const std::function<void(Result)>& then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"X-Emby-Token: " + c.getUser().access_token};

        try {
            auto resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{});
            if (resp.empty()) return;
            auto j = nlohmann::json::parse(resp).get<Result>();
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

        try {
            auto resp = HTTP::post(c.getUrl() + url, data.dump(), header, HTTP::Timeout{});
            if (resp.empty()) return;
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, ex.what()));
        }
    });
}

};  // namespace jellyfin

#include "jellyfin/system.hpp"
#include "jellyfin/media.hpp"
