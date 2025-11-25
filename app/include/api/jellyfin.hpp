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
        HTTP::Header header = {c.getAuth(c.getToken())};

        try {
            auto resp = HTTP::get(c.getUrl() + url, header, HTTP::Timeout{});
            if (resp.empty()) return;
            auto j = nlohmann::json::parse(resp).get<Result>();
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

template <typename Then, typename... Args>
inline void postJSON(const nlohmann::json& data, Then then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url, data]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {"Content-Type: application/json", c.getAuth(c.getToken())};

        try {
            auto resp = HTTP::post(c.getUrl() + url, data.dump(), header, HTTP::Timeout{});
            if (resp.empty()) return;
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

template <typename Result, typename... Args>
inline void deleteJSON(const std::function<void(Result)>& then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    brls::async([then, error, url]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {c.getAuth(c.getToken())};

        try {
            HTTP s;
            std::ostringstream body;
            HTTP::set_option(s, header, HTTP::Timeout{});
            s._delete(c.getUrl() + url, &body);
            auto j = nlohmann::json::parse(body.str()).get<Result>();
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

template <typename T>
struct Result {
    std::vector<T> Items;
    long TotalRecordCount = 0;
    long StartIndex = 0;
};

template <typename T>
inline void to_json(nlohmann::json& nlohmann_json_j, const Result<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, Items, TotalRecordCount, StartIndex))
}

template <typename T>
inline void from_json(const nlohmann::json& nlohmann_json_j, Result<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, Items, TotalRecordCount, StartIndex))
}

};  // namespace jellyfin

#include "jellyfin/system.hpp"
#include "jellyfin/media.hpp"