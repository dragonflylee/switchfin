#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#include <filesystem>
#elif defined(__PSV__)
#include <psp2/vshbridge.h>
#elif defined(ANDROID)
#include <SDL2/SDL.h>
#include <jni.h>
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
#elif defined(__PSV__)
    return "PSVita";
#elif defined(__PS4__)
    return "PS4";
#elif defined(ANDROID)
    return "Android";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    if (getenv("SteamDeck")) return "SteamDeck";
    return "Linux";
#elif defined(_WIN32)
#if defined(_M_ARM64)
    return "Windows-arm64";
#else
    return "Windows";
#endif
#else
#error "Unsupport platform"
#endif
}

std::string AppVersion::getDeviceName() {
#ifdef __SWITCH__
    SetSysDeviceNickName nick;
    if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
        return nick.nickname;
    }
#elif defined(__PSV__)
    if (vshSblAimgrIsGenuineDolce()) {
        return "PSTV";
    } else if (vshSblAimgrIsGenuineVITA()) {
        char cid[0x20];
        if (_vshSblAimgrGetConsoleId(cid) >= 0) {
            if (cid[7] == 0x14 || cid[7] == 0x18) {
                return "PSVita Slim";
            }
        }
        return "PSVita";
    }
#elif defined(ANDROID)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jclass clazz = env->FindClass("android/os/Build");
    if (clazz) {
        jfieldID fid = env->GetStaticFieldID(clazz, "MODEL", "Ljava/lang/String;");
        jstring jname = (jstring)env->GetStaticObjectField(clazz, fid);
        const char* name = env->GetStringUTFChars(jname, nullptr);
        std::string device_name = name;
        env->ReleaseStringUTFChars(jname, name);
        env->DeleteLocalRef(jname);
        env->DeleteLocalRef(clazz);
        return device_name;
    }
#elif defined(_WIN32)
    DWORD bufsize = MAX_PATH;
    std::wstring buf(bufsize, '\0');
    if (GetComputerNameW(buf.data(), &bufsize)) {
        std::string name(bufsize * 3, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf.data(), bufsize, name.data(), name.size(), nullptr, nullptr);
        return name.data();
    }
#elif defined(__APPLE__)
    CFStringRef nameRef = SCDynamicStoreCopyComputerName(nullptr, nullptr);
    if (nameRef) {
        std::vector<char> name(CFStringGetLength(nameRef) * 3);
        CFStringGetCString(nameRef, name.data(), name.size(), kCFStringEncodingUTF8);
        CFRelease(nameRef);
        return name.data();
    }
#elif defined(__linux__)
    char name[256];
    if (!gethostname(name, sizeof(name))) {
        return name;
    }
#endif
    return fmt::format("{} for {}", getPackageName(), getPlatform());
}

bool AppVersion::needUpdate(std::string latestVersion) { return false; }

void AppVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    if (!AppVersion::updating->load()) {
        Dialog::cancelable("main/setting/others/updating"_i18n, [] { AppVersion::updating->store(true); });
        return;
    }
    ThreadPool::instance().submit([showUpToDateDialog](HTTP& s) {
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
                    ThreadPool::instance().submit([latest_ver](HTTP& s) {
                        std::string conf_dir = AppConfig::instance().configDir();
                        std::string pkg_name = AppVersion::getPackageName();
                        std::string path = fmt::format("{}/{}_{}.nro", conf_dir, pkg_name, latest_ver);
                        std::string url = fmt::format(
                            "https://github.com/{}/releases/download/{}/{}.nro", git_repo, latest_ver, pkg_name);
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
