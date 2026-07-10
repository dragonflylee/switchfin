#include <borealis.hpp>
#include "utils/image_cache.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/http.hpp"
#include "api/plex.hpp"

#include <mutex>
#include <unordered_set>

namespace ImageCache {

// In-memory index of cached keys so has() never touches the disk on the UI
// thread (it is called from every Image::load). Populated at init(), kept in
// sync by store()/remove().
static std::mutex g_mutex;
static std::unordered_set<std::string> g_keys;

std::string dir() { return AppConfig::instance().configDir() + "/downloads/art"; }

std::string localPath(const std::string& pathOrUrl) { return dir() + "/" + key(pathOrUrl); }

void init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_keys.clear();
    try {
        std::string d = dir();
        if (!fs::exists(d)) return;
        for (auto& e : fs::directory_iterator(d)) {
            std::string name = e.path().filename().string();
            // keys are 16 hex chars, no extension; skip .part temporaries
            if (name.find('.') == std::string::npos) g_keys.insert(name);
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("ImageCache: init scan failed: {}", ex.what());
    }
}

bool has(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_keys.count(key(pathOrUrl)) > 0;
}

bool store(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return false;

    std::string local = localPath(pathOrUrl);
    try {
        if (fs::exists(local)) {  // already cached
            std::lock_guard<std::mutex> lock(g_mutex);
            g_keys.insert(key(pathOrUrl));
            return true;
        }
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
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_keys.insert(key(pathOrUrl));
        }
        return true;
    } catch (const std::exception& e) {
        brls::Logger::warning("ImageCache: store failed {}: {}", pathOrUrl, e.what());
        return false;
    }
}

void remove(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_keys.erase(key(pathOrUrl));
    }
    try {
        std::string local = localPath(pathOrUrl);
        if (fs::exists(local)) fs::remove(local);
    } catch (const std::exception& e) {
        brls::Logger::warning("ImageCache: remove failed {}: {}", pathOrUrl, e.what());
    }
}

}  // namespace ImageCache
