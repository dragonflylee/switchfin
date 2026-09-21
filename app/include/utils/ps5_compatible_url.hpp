#pragma once

#include "api/jellyfin/media.hpp"
#include "utils/ps5_playback_profile.hpp"
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace ps5::playback {
namespace compatible_url_detail {
inline int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
inline bool validText(std::string_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c <= 32 || c == 127 || c == '\\') return false;
        if (c == '%') {
            if (i + 2 >= text.size() || hex(text[i + 1]) < 0 || hex(text[i + 2]) < 0) return false;
            i += 2;
        }
    }
    return true;
}
inline std::string decodedKey(std::string_view key) {
    std::string result;
    for (size_t i = 0; i < key.size(); ++i) {
        char c = key[i];
        if (c == '%') { c = static_cast<char>(hex(key[i + 1]) * 16 + hex(key[i + 2])); i += 2; }
        else if (c == '+') c = ' ';
        result += lower(c);
    }
    return result;
}
inline size_t schemeLength(std::string_view url) {
    for (const auto prefix : {std::string_view("https://"), std::string_view("http://")}) {
        if (url.size() < prefix.size()) continue;
        bool matches = true;
        for (size_t i = 0; i < prefix.size(); ++i) matches &= lower(url[i]) == prefix[i];
        if (matches) return prefix.size();
    }
    return 0;
}
inline bool validAbsolute(std::string_view url) {
    const auto begin = schemeLength(url);
    if (!begin || !validText(url)) return false;
    const auto end = url.find_first_of("/?#", begin);
    auto authority = url.substr(begin, end == std::string_view::npos ? end : end - begin);
    if (authority.empty()) return false;
    std::string_view port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || close < 3) return false;
        const auto host = authority.substr(1, close - 1);
        in6_addr address{};
        if (inet_pton(AF_INET6, std::string(host).c_str(), &address) != 1) return false;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return false;
            port = authority.substr(close + 2);
            if (port.empty()) return false;
        }
    } else {
        const auto colon = authority.find(':');
        const auto host = authority.substr(0, colon);
        if (host.empty()) return false;
        for (char c : host)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
        if (colon != std::string_view::npos) {
            port = authority.substr(colon + 1);
            if (port.empty()) return false;
        }
    }
    unsigned value = 0;
    for (char c : port) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > 65535) return false;
    }
    return port.empty() || value != 0;
}
inline std::string origin(std::string_view url) {
    auto result = std::string(url.substr(0, url.find_first_of("/?#", schemeLength(url))));
    for (auto& c : result) c = lower(c);
    const auto port = schemeLength(url) == 8 ? std::string_view(":443") : std::string_view(":80");
    if (result.size() >= port.size() && result.compare(result.size() - port.size(), port.size(), port) == 0)
        result.resize(result.size() - port.size());
    return result;
}
inline bool signatureKey(std::string_view key) {
    return key == "sig" || key == "signature" || key == "hmac" || key == "key-pair-id" ||
        key == "awsaccesskeyid" || key.rfind("x-amz-", 0) == 0 || key.rfind("x-goog-", 0) == 0;
}
} // namespace compatible_url_detail

// Jellyfin's HLS endpoint accepts Framerate directly. MaxFramerate alone
// leaves the encoder rate unset if server probing did not find a source rate
// (EncodingHelper.GetFramerateParam, Jellyfin 10.8.13/10.10.7/10.11.0).
// The caller owns the compatible-only restriction and no-copy request policy.
inline std::optional<std::string> compatibleMediaUrl(const std::string& server, const std::string& route,
    const jellyfin::Source& original, Backend backend) {
    using namespace compatible_url_detail;
    if (route.empty() || !validText(route)) return std::nullopt;
    std::string url;
    if (schemeLength(route)) url = route;
    else {
        // Match the existing server/base-path join, without guessing how to
        // resolve authority-relative, query-only, or another scheme's URL.
        if (!validAbsolute(server) || server.find_first_of("?#") != std::string::npos ||
            route.rfind("//", 0) == 0 || route.front() == '?' || route.front() == '#' ||
            route.substr(0, route.find_first_of("/?#")).find(':') != std::string::npos) return std::nullopt;
        url = server;
        if (url.back() == '/' && route.front() == '/') url.pop_back();
        else if (url.back() != '/' && route.front() != '/') url += '/';
        url += route;
    }
    if (!validAbsolute(url)) return std::nullopt;
    bool video = false;
    for (const auto& stream : original.MediaStreams) {
        if (stream.Type != jellyfin::streamTypeVideo) continue;
        video = true;
        const auto valid = [](const auto& fps) { return fps && std::isfinite(*fps) && *fps > 0; };
        if (!valid(stream.AverageFrameRate) && !valid(stream.RealFrameRate)) { video = false; break; }
    }
    if (video) return url; // Keep the known source's cadence and original query bytes.

    const auto fragment = url.find('#');
    const auto end = fragment == std::string::npos ? url.size() : fragment;
    const auto query = url.find('?');
    const bool hasQuery = query != std::string::npos && query < end;
    // Query mutation is supported for server-issued Jellyfin HLS only. Do not
    // alter external/CDN routes or known signatures. A custom signing scheme
    // is not qualified by this blacklist and requires separate integration.
    if (!validAbsolute(server) || server.find_first_of("?#") != std::string::npos || origin(server) != origin(url))
        return std::nullopt;
    const auto pathEnd = hasQuery ? query : end;
    const auto pathBegin = url.find('/', schemeLength(url));
    if (pathBegin == std::string::npos || pathBegin >= pathEnd || pathEnd - pathBegin < 6) return std::nullopt;
    auto extension = url.substr(pathEnd - 5, 5);
    for (auto& c : extension) c = lower(c);
    if (extension != ".m3u8") return std::nullopt;
    std::string result = url.substr(0, hasQuery ? query : end);
    result += '?';
    if (hasQuery) {
        size_t begin = query + 1;
        while (begin <= end) {
            auto next = url.find('&', begin);
            if (next == std::string::npos || next > end) next = end;
            const auto pair = std::string_view(url).substr(begin, next - begin);
            const auto key = decodedKey(pair.substr(0, pair.find('=')));
            if (signatureKey(key)) return std::nullopt;
            if (key != "framerate") {
                result.append(pair);
                result += '&';
            }
            if (next == end) break;
            begin = next + 1;
        }
    }
    result += "Framerate=" + std::to_string(capabilities(backend).maxInputFramerate);
    if (fragment != std::string::npos) result += url.substr(fragment);
    return result;
}
} // namespace ps5::playback
