#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/http.hpp"
#include <borealis/core/thread.hpp>

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

std::string AppVersion::git_commit = STR(BUILD_TAG_SHORT);
std::string AppVersion::git_tag = STR(BUILD_TAG_VERSION);
std::string AppVersion::pkg_name = STR(BUILD_PACKAGE_NAME);

std::string AppVersion::getVersion() { return git_tag.empty() ? "dev-" + git_commit : git_tag; }

std::string AppVersion::getPlatform() {
#if __SWITCH__
    return "NX";
#elif __APPLE__
    return "macOS";
#elif __linux__
    return "Linux";
#elif _WIN32
    return "Windows";
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
            std::string url = "https://api.github.com/repos/dragonflylee/switchfin/releases/latest";
            auto resp = HTTP::get(url, HTTP::Timeout{1000});
            nlohmann::json j = nlohmann::json::parse(resp);
            brls::Logger::info("checkUpdate {}", j.at("name").get<std::string>());
        } catch (const std::exception& ex) {
        }
    });
}