/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include "http.hpp"
#include "utils/config.hpp"
#ifdef PS5_NATIVE_GPU
#include <optional>
#include <sstream>
#include <type_traits>
#endif

namespace jellyfin {

using OnError = std::function<void(const std::string&)>;

#ifdef PS5_NATIVE_GPU
struct RequestContext {
    std::string server;
    std::string user;
    HTTP::Header headers;
    HTTP::Cancel cancel;
    HTTP::Cancel ownerCancel;

    static RequestContext capture() {
        auto& config = AppConfig::instance();
        return {config.getUrl(), config.getUserId(), {config.getAuth(config.getToken())},
            config.requestCancellation(), {}};
    }
    bool cancelled() const { return (cancel && cancel->load()) || (ownerCancel && ownerCancel->load()); }
    HTTP::Cancel transportCancellation() const { return ownerCancel ? ownerCancel : cancel; }
};

// Work and JSON conversion run on the worker. Completion always runs on the UI
// thread, outside the worker's exception handler, and releases ASYNC_RETAIN even
// on an empty response or cancellation. No worker reads mutable AppConfig data.
template <typename Result, typename Work, typename Then>
inline void request(RequestContext context, Work work, Then then, OnError error) {
    brls::async([context, work, then, error]() {
        std::optional<Result> result;
        std::string failure;
        try {
            if (context.cancelled()) throw std::runtime_error("Request cancelled.");
            result.emplace(work(context));
        } catch (const nlohmann::json::exception&) {
            failure = "The server returned an invalid response.";
        } catch (const std::exception& ex) {
            failure = ex.what();
        }
        brls::sync([context, result = std::move(result), failure, then, error]() mutable {
            if (context.cancelled()) {
                if (error) error("Request cancelled.");
            } else if (result) {
                // Existing fire-and-forget callers use [](...) {}. Passing a
                // non-POD JSON object through C varargs aborts on native libc++.
                if constexpr (std::is_invocable_v<Then>) then();
                else then(std::move(*result));
            } else if (error) {
                error(failure);
            }
        });
    });
}

template <typename Result>
inline Result fetchJSON(const RequestContext& context, const std::string& path) {
    if (context.cancelled()) throw std::runtime_error("Request cancelled.");
    auto response = HTTP::get(context.server + path, context.headers, HTTP::Timeout{}, context.transportCancellation());
    if (response.empty()) throw std::runtime_error("The server returned an empty response.");
    return nlohmann::json::parse(response).get<Result>();
}

#endif
template <typename Result, typename... Args>
inline void getJSON(const std::function<void(Result)>& then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
#ifdef PS5_NATIVE_GPU
    request<Result>(RequestContext::capture(),
        [url](const RequestContext& context) { return fetchJSON<Result>(context, url); }, then, error);
#else
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
#endif
}

#ifdef PS5_NATIVE_GPU
template <typename Result = nlohmann::json, typename Then, typename... Args>
inline void postJSONTo(RequestContext context, const nlohmann::json& data, Then then, OnError error,
    std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
    request<Result>(std::move(context), [url, data](const RequestContext& context) -> Result {
        auto headers = context.headers;
        headers.push_back("Content-Type: application/json");
        auto response = HTTP::post(context.server + url, data.dump(), headers, HTTP::Timeout{}, context.transportCancellation());
        // Jellyfin's session/report endpoints legitimately return 204 No Content.
        if (response.empty()) {
            if constexpr (std::is_same_v<Result, nlohmann::json>) return {};
            else throw std::runtime_error("The server returned an empty response.");
        }
        return nlohmann::json::parse(response).get<Result>();
    }, then, error);
}

template <typename Result = nlohmann::json, typename Then, typename... Args>
#else
template <typename Then, typename... Args>
#endif
inline void postJSON(const nlohmann::json& data, Then then, OnError error, std::string_view fmt, Args&&... args) {
#ifdef PS5_NATIVE_GPU
    postJSONTo<Result>(RequestContext::capture(), data, then, error, fmt, std::forward<Args>(args)...);
#else
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
#endif
}

template <typename Result, typename... Args>
inline void deleteJSON(const std::function<void(Result)>& then, OnError error, std::string_view fmt, Args&&... args) {
    std::string url = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);
#ifdef PS5_NATIVE_GPU
    request<Result>(RequestContext::capture(), [url](const RequestContext& context) {
#else
    brls::async([then, error, url]() {
        auto& c = AppConfig::instance();
        HTTP::Header header = {c.getAuth(c.getToken())};

        try {
#endif
            HTTP s;
            std::ostringstream body;
#ifdef PS5_NATIVE_GPU
            HTTP::set_option(s, context.headers, HTTP::Timeout{}, context.cancel);
            s._delete(context.server + url, &body);
            // Typed DELETE callers expect the returned DTO; an empty body must
            // complete as an error instead of abandoning their lifetime token.
            if (body.str().empty()) throw std::runtime_error("The server returned an empty response.");
            return nlohmann::json::parse(body.str()).get<Result>();
    }, then, error);
#else
            HTTP::set_option(s, header, HTTP::Timeout{});
            s._delete(c.getUrl() + url, &body);
            auto j = nlohmann::json::parse(body.str()).get<Result>();
            brls::sync(std::bind(std::move(then), std::move(j)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
#endif
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
#ifdef PS5_NATIVE_GPU
#include "jellyfin/media.hpp"
#else
#include "jellyfin/media.hpp"
#endif
