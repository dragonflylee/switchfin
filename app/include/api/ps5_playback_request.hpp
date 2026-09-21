#pragma once

#include <api/http.hpp>
#include <api/jellyfin/media.hpp>
#include <borealis/core/thread.hpp>
#include <utils/config.hpp>
#include <functional>
#include <optional>
#include <utility>

namespace ps5::playback {

enum class Route { Unavailable, Remote, Direct, Converted };

inline Route routeFor(const jellyfin::Source& source, bool forceDirectPlay, bool convertedOnly = false) {
    if (convertedOnly)
        return source.SupportsTranscoding && !source.TranscodingUrl.empty() ? Route::Converted : Route::Unavailable;
    if (source.IsRemote && forceDirectPlay) return source.Path.empty() ? Route::Unavailable : Route::Remote;
    if ((source.SupportsDirectPlay || forceDirectPlay) && !source.Id.empty()) return Route::Direct;
    // Jellyfin returns remux and re-encode URLs through TranscodingUrl. Its
    // SupportsDirectStream flag alone does not prove the video was copied.
    if ((source.SupportsTranscoding || source.SupportsDirectStream) && !source.TranscodingUrl.empty())
        return Route::Converted;
    return Route::Unavailable;
}

inline std::string mediaUrl(const std::string& server, const std::string& route) {
    if (route.empty()) return {};
    if (route.rfind("https://", 0) == 0 || route.rfind("http://", 0) == 0) return route;
    if (!server.empty() && server.back() == '/' && route.front() == '/') return server + route.substr(1);
    return server + (server.empty() || server.back() == '/' || route.front() == '/' ? "" : "/") + route;
}

// PlaybackInfo requires a typed response, unlike fire-and-forget session
// reports. Snapshot authentication before queuing work and parse the complete
// model on the worker so JSON conversion cannot throw later on the UI thread.
template <typename Then, typename... Args>
inline void postInfo(const nlohmann::json& data, Then then,
    std::function<void(const std::string&)> error, std::string_view format, Args&&... args) {
    auto& config = AppConfig::instance();
    const std::string url = config.getUrl() + fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
    const HTTP::Header headers = {"Content-Type: application/json", config.getAuth(config.getToken())};
    const auto cancel = config.requestCancellation();
    brls::async([data, then, error, url, headers, cancel]() {
        std::optional<jellyfin::PlaybackResult> result;
        std::string failure;
        try {
            if (cancel->load()) throw std::runtime_error("Request cancelled.");
            const auto response = HTTP::post(url, data.dump(), headers, HTTP::Timeout{}, cancel);
            if (response.empty()) {
                failure = "The server returned an empty playback response.";
            } else {
                result = nlohmann::json::parse(response).get<jellyfin::PlaybackResult>();
            }
        } catch (const nlohmann::json::exception&) {
            // JSON diagnostics may contain response text or authenticated URLs.
            failure = "The server returned an invalid playback response.";
        } catch (const std::exception& ex) {
            // HTTP reports curl/status errors without including request data.
            failure = ex.what();
        }
        if (result) {
            brls::sync([then, error, cancel, result = std::move(*result)]() {
                if (cancel->load()) { if (error) error("Request cancelled."); }
                else then(result);
            });
        } else if (error) {
            brls::sync([error, failure]() { error(failure); });
        }
    });
}

}  // namespace ps5::playback
