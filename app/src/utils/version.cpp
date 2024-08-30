#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#include <filesystem>
#elif defined(__APPLE__)
#include <SystemConfiguration/SystemConfiguration.h>
#elif defined(__linux__)
#include <borealis/platforms/desktop/steam_deck.hpp>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

std::string AppVersion::getCommit() { return STR(BUILD_TAG_SHORT); }

std::string AppVersion::getPlatform() {
#ifdef __SWITCH__
    return "NX";
#elif defined(PS4)
    return "PS4";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    if (getenv("SteamDeck")) return "SteamDeck";
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#endif
}

std::string AppVersion::getDeviceName() {
#ifdef __SWITCH__
    SetSysDeviceNickName nick;
    if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
        return nick.nickname;
    }
#elif defined(_WIN32)
    DWORD nSize = 128;
    std::vector<WCHAR> buf(nSize);
    if (GetComputerNameW(buf.data(), &nSize)) {
        std::string name;
        name.resize(nSize);
        WideCharToMultiByte(CP_UTF8, 0, buf.data(), nSize, name.data(), name.size(), nullptr, nullptr);
        return name;
    }
#elif defined(__APPLE__)
    CFStringRef nameRef = SCDynamicStoreCopyComputerName(nullptr, nullptr);
    if (nameRef) {
        std::vector<char> name(CFStringGetLength(nameRef) * 3);
        CFStringGetCString(nameRef, name.data(), name.size(), kCFStringEncodingUTF8);
        CFRelease(nameRef);
        return name.data();
    }
#else
    std::vector<char> buf(128);
    if (gethostname(buf.data(), buf.size()) > 0) {
        return buf.data();
    }
#endif
    return getPackageName();
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
            auto resp = HTTP::get(url, HTTP::Timeout{});
            nlohmann::json j = nlohmann::json::parse(resp);
            std::string latest_ver = j.at("tag_name").get<std::string>();
            if (latest_ver.compare(getVersion()) <= 0) {
                brls::Logger::info("App is up to date");
                if (showUpToDateDialog) brls::sync([]() { Dialog::show("main/setting/others/up2date"_i18n); });
                return;
            }

            brls::sync([latest_ver]() {
                std::string title = brls::getStr("main/setting/others/upgrade", latest_ver);
                auto dialog = new brls::Dialog(title);
                dialog->addButton("hints/cancel"_i18n, []() {
                    auto& conf = AppConfig::instance();
                    conf.setItem(AppConfig::APP_UPDATE, getVersion());
                });
#ifdef __SWITCH__
                dialog->addButton("hints/ok"_i18n, [latest_ver]() {
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
                dialog->addButton("hints/ok"_i18n, [url] { brls::Application::getPlatform()->openBrowser(url); });
#endif
                dialog->open();
            });
        } catch (const std::exception& ex) {
            brls::Logger::error("checkUpdate failed: {}", ex.what());
        }
    });
}
