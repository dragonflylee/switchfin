#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/http.hpp"

using namespace brls::literals;

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
    SetSysDeviceNickName nick;
    if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
        name = nick.nickname;
    }
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
    brls::async([showUpToDateDialog]() {
        try {
            std::string url = fmt::format("https://api.github.com/repos/{}/releases/tags/latest", git_repo);
            auto resp = HTTP::get(url, HTTP::Timeout{1000});
            nlohmann::json j = nlohmann::json::parse(resp);
            std::string latest_ver = j.at("tag_name").get<std::string>();
            if (latest_ver.compare(git_tag) <= 0) {
                brls::Logger::info("App is up to date");
                if (showUpToDateDialog) brls::sync([]() { Dialog::show("main/setting/others/up2date"_i18n); });
                return;
            }

            Dialog::cancelable(fmt::format(fmt::string_view("main/setting/others/upgrade"_i18n), latest_ver), [latest_ver] {
#ifdef __SWITCH__
#else
                std::string url = fmt::format("https://github.com/{}/releases/tag/{}", git_repo, latest_ver);
                brls::Application::getPlatform()->openBrowser(url);
#endif
            });
        } catch (const std::exception& ex) {
            brls::Logger::error("checkUpdate failed: {}", ex.what());
        }
    });
}