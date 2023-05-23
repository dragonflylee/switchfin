#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/http.hpp"

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

std::string AppVersion::git_commit = STR(BUILD_TAG_SHORT);
std::string AppVersion::git_tag = STR(BUILD_TAG_VERSION);

std::string AppVersion::getVersion() {
    if (git_tag.empty())
        return "dev-" + git_commit;
    else
        return git_tag;
}

std::string AppVersion::getPlatform() {
#if __APPLE__
    return "Jellyfin for macOS";
#elif __linux__
    return "Jellyfin for Linux";
#elif _WIN32
    return "Jellyfin for Windows";
#elif __SWITCH__
    return "Jellyfin for Switch";
#else
#error "unsupport platform"
#endif
}

std::string AppVersion::getDeviceName() {
    std::vector<char> name(128);
#ifdef _WIN32
    DWORD len = name.size();
    GetComputerNameA(name.data(), &len);
#else
    gethostname(name.data(), name.size());
#endif
    return std::string(name.data());
}

bool AppVersion::needUpdate(std::string latestVersion) { return false; }

void AppVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    std::string url = "https://api.github.com/repos/jellyfin/jellyfin/releases/latest";
    HTTP::get_async(
        [](const std::string& resp) {
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::Logger::info("checkUpdate {}", j.at("name").get<std::string>());
        },
        nullptr, url, HTTP::Timeout{1000});
}