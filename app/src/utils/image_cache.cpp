#include <borealis.hpp>
#include "utils/image_cache.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/http.hpp"
#include "api/plex.hpp"

namespace ImageCache {

std::string dir() { return AppConfig::instance().configDir() + "/downloads/art"; }

std::string localPath(const std::string& pathOrUrl) { return dir() + "/" + key(pathOrUrl); }

bool has(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return false;
    try {
        return fs::exists(localPath(pathOrUrl));
    } catch (...) {
        return false;
    }
}

bool store(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return false;

    std::string local = localPath(pathOrUrl);
    try {
        if (fs::exists(local)) return true;  // already cached
    } catch (...) {
    }

    // relative Plex path -> original file on the server (no transcode, one file
    // per path whatever the requested display size). Absolute URL stays as-is.
    std::string url;
    if (pathOrUrl.rfind("http", 0) == 0) {
        url = pathOrUrl;
    } else {
        auto& conf = AppConfig::instance();
        url = plex::withToken(conf.getUrl() + pathOrUrl, conf.getToken());
    }

    try {
        std::string d = dir();
        if (!fs::exists(d)) fs::create_directories(d);
        // write to a temp then rename so a half-written file never reads as
        // present (has() must only be true for complete assets)
        std::string tmp = local + ".part";
        HTTP::download(url, tmp, HTTP::Timeout{});
        fs::rename(tmp, local);
        return true;
    } catch (const std::exception& e) {
        brls::Logger::warning("ImageCache: store failed {}: {}", pathOrUrl, e.what());
        return false;
    }
}

void remove(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return;
    try {
        std::string local = localPath(pathOrUrl);
        if (fs::exists(local)) fs::remove(local);
    } catch (const std::exception& e) {
        brls::Logger::warning("ImageCache: remove failed {}: {}", pathOrUrl, e.what());
    }
}

}  // namespace ImageCache
