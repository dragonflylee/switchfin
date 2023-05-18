#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/http.hpp"
#include <nlohmann/json.hpp>

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

AppVersion::AppVersion() {
    git_commit = std::string(STR(BUILD_TAG_SHORT));
    git_tag = std::string(STR(BUILD_TAG_VERSION));
}

std::string AppVersion::getPlatform() {
#if __APPLE__
    return "macOS";
#elif __linux__
    return "Linux";
#elif _WIN32
    return "Windows";
#elif __SWITCH__
    return "NX";
#else
#error "unsupport platform"
#endif
}

bool AppVersion::needUpdate(std::string latestVersion) { return false; }

void AppVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    std::string url = "https://api.github.com/repos/jellyfin/jellyfin/releases/latest";
    HTTP::get_async(
        [](const std::string& resp) {
            auto j = nlohmann::json::parse(resp);
            brls::Logger::info("checkUpdate {}", j.at("name").get<std::string>());
        },
        url, HTTP::Header{"x-requested-with: XMLHttpRequest"});
}