#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/http.hpp"
#include <borealis/core/thread.hpp>

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
#if __SWITCH__
    return STR(BUILD_PACKAGE_NAME) " for Switch";
#elif __APPLE__
    return STR(BUILD_PACKAGE_NAME) " for macOS";
#elif __linux__
    return STR(BUILD_PACKAGE_NAME) " for Linux";
#elif _WIN32
    return STR(BUILD_PACKAGE_NAME) " for Windows";
#endif
}

std::string AppVersion::getDeviceName() {
    std::string name;
#if __SWITCH__
#elif _WIN32
    DWORD nSize = 128;
    std::vector<WCHAR> buf(nSize);
    GetComputerNameW(buf.data(), &nSize);
    name.resize(nSize);
    WideCharToMultiByte(CP_UTF8, 0, buf.data(), nSize, name.data(), name.size(), nullptr, nullptr);
#else
    gethostname(name.data(), name.size());
#endif
    return std::string(name.data());
}

bool AppVersion::needUpdate(std::string latestVersion) { return false; }

void AppVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    brls::async([]() {
        try {
            std::string url = "https://api.github.com/repos/jellyfin/jellyfin/releases/latest";
            auto resp = HTTP::get(url, HTTP::Timeout{1000});
            nlohmann::json j = nlohmann::json::parse(std::get<1>(resp));
            brls::Logger::info("checkUpdate {}", j.at("name").get<std::string>());
        } catch (const std::exception& ex) {
        }
    });
}