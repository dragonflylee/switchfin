#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#include <filesystem>
#endif
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/thread.hpp"
#include "api/http.hpp"

using namespace brls::literals;

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

std::string AppVersion::getVersion() { return STR(APP_VERSION); }

std::string AppVersion::getPackageName() { return STR(BUILD_PACKAGE_NAME); }

std::string AppVersion::getCommit()  { return STR(BUILD_TAG_SHORT); }

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
    if (!AppVersion::updating->load()) {
        Dialog::cancelable("main/setting/others/updating"_i18n, [] { AppVersion::updating->store(true); });
        return;
    }
    ThreadPool::instance().submit([showUpToDateDialog]() {
        try {
            std::string url = fmt::format("https://api.github.com/repos/{}/releases/latest", git_repo);
            auto resp = HTTP::get(url, HTTP::Timeout{default_timeout});
            nlohmann::json j = nlohmann::json::parse(resp);
            std::string latest_ver = j.at("tag_name").get<std::string>();
            if (latest_ver.compare(getVersion()) <= 0) {
                brls::Logger::info("App is up to date");
                if (showUpToDateDialog) brls::sync([]() { Dialog::show("main/setting/others/up2date"_i18n); });
                return;
            }

            brls::sync([latest_ver]() {
                std::string title = fmt::format(fmt::runtime("main/setting/others/upgrade"_i18n), latest_ver);
#ifdef __SWITCH__
                Dialog::cancelable(title, [latest_ver]() {
                    AppVersion::updating->store(false);
                    ThreadPool::instance().submit([latest_ver]() {
                        std::string conf_dir = AppConfig::instance().configDir();
                        std::string pkg_name = AppVersion::getPackageName();
                        std::string path = fmt::format("{}/{}_{}.nro", conf_dir, pkg_name, latest_ver);
                        std::string url = fmt::format(
                            "https://github.com/{}/releases/download/{}/Switchfin.nro", git_repo, latest_ver);
                        try {
                            HTTP::download(url, path, HTTP::Timeout{-1}, AppVersion::updating);
                            romfsExit();

                            std::string target = fmt::format("{}/{}.nro", conf_dir, pkg_name);
                            std::filesystem::remove(target);
                            std::filesystem::rename(path, target);
                            Dialog::quitApp(true);
                        } catch (const std::exception& ex) {
                            std::filesystem::remove(path);
                            AppVersion::updating->store(true);
                            std::string msg = fmt::format("{}: {}", path, ex.what());
                            brls::sync([msg]() { Dialog::show(msg); });
                        }
                    });
                });
#else
                std::string url = fmt::format("https://github.com/{}/releases/tag/{}", git_repo, latest_ver);
                Dialog::cancelable(title, [url] { brls::Application::getPlatform()->openBrowser(url); });
#endif
            });
        } catch (const std::exception& ex) {
            brls::Logger::error("checkUpdate failed: {}", ex.what());
        }
    });
}