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
#include <algorithm>
#include <cstdio>
#include <thread>
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/thread.hpp"
#include "utils/misc.hpp"
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

bool AppVersion::needUpdate(std::string latestVersion) {
    // Release tags are "vX.Y.Z" while APP_VERSION is "X.Y.Z": strip the prefix and compare numerically
    auto parse = [](const std::string& s, int out[3]) {
        size_t start = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
        return std::sscanf(s.c_str() + start, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
    };
    int latest[3], current[3];
    if (!parse(latestVersion, latest) || !parse(getVersion(), current))
        return latestVersion.compare(getVersion()) > 0;
    return std::lexicographical_compare(current, current + 3, latest, latest + 3);
}

#ifdef __SWITCH__
/// Downloads the release NRO into a temporary file in configDir, verifies
/// its size, then replaces the running NRO (real path provided by
/// hbloader/the forwarder via argv[0], cf. AppVersion::nro_path).
/// Progress is shown in a dialog with a cancel button.
static void startUpdate(const std::string& latest_ver, const std::string& url, int64_t size) {
    AppVersion::updating->store(false);

    brls::Style style = brls::Application::getStyle();
    auto* label = new brls::Label();
    label->setFontSize(style["brls/dialog/fontSize"]);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setSingleLine(false);
    label->setText(brls::getStr("main/setting/others/downloading", latest_ver, 0));

    auto* box = new brls::Box();
    box->addView(label);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setPadding(style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"],
        style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"]);

    auto* dialog = new brls::Dialog(box);
    dialog->setCancelable(false);
    // the click closes the dialog (cf. Dialog::buttonClick): `dismissed`
    // prevents a double close and protects `label` (destroyed with the
    // dialog) from progress updates still queued in brls::sync
    auto dismissed = std::make_shared<std::atomic_bool>(false);
    dialog->addButton("hints/cancel"_i18n, [dismissed]() {
        dismissed->store(true);
        AppVersion::updating->store(true);  // true = cancel the transfer, cf. HTTP::easy_progress_cb
    });
    dialog->open();

    ThreadPool::instance().submit([latest_ver, url, size, label, dialog, dismissed](HTTP& s) {
        std::string conf_dir = AppConfig::instance().configDir();
        std::string pkg_name = AppVersion::getPackageName();
        std::string path = fmt::format("{}/{}_{}.nro", conf_dir, pkg_name, latest_ver);

        auto finish = [dialog, dismissed](std::function<void()> then) {
            brls::sync([dialog, dismissed, then]() {
                if (dismissed->exchange(true))
                    then();
                else
                    dialog->close(then);
            });
        };

        auto last = std::make_shared<std::chrono::steady_clock::time_point>();
        HTTP::Progress::Callback progress = [latest_ver, label, dismissed, last](curl_off_t total, curl_off_t now) {
            auto tp = std::chrono::steady_clock::now();
            if (total <= 0 || tp - *last < std::chrono::milliseconds(500)) return;
            *last = tp;
            int percent = static_cast<int>(now * 100 / total);
            brls::sync([label, dismissed, latest_ver, percent]() {
                if (!dismissed->load())
                    label->setText(brls::getStr("main/setting/others/downloading", latest_ver, percent));
            });
        };

        try {
            HTTP::download(url, path, HTTP::Timeout{-1}, AppVersion::updating, progress);

            // size verified BEFORE overwriting the running NRO
            auto actual = std::filesystem::file_size(path);
            if (size > 0 && actual != static_cast<std::uintmax_t>(size))
                throw std::runtime_error(fmt::format("incomplete download ({}/{} bytes)", actual, size));

            // the romfs is mapped from the NRO file: unmount before replacing it
            romfsExit();
            std::string target = AppVersion::nro_path;
            if (target.size() < 4 || target.compare(target.size() - 4, 4, ".nro") != 0)
                target = fmt::format("{}/{}.nro", conf_dir, pkg_name);
            std::filesystem::remove(target);
            std::filesystem::rename(path, target);
            finish([]() { Dialog::quitApp(true, "main/setting/others/updated"_i18n); });
        } catch (const std::exception& ex) {
            std::filesystem::remove(path);
            bool canceled = AppVersion::updating->load();
            AppVersion::updating->store(true);
            if (canceled) return;  // user cancellation, not a failure
            std::string msg = ex.what();
            finish([msg]() { Dialog::show(msg); });
        }
    });
}
#endif

// Update popup content: the version prompt + the new release's notes (GitHub
// `body`, light-markdown cleaned, scrollable). The notes row is hidden when
// the release carries none. The full history lives in Settings ▸ Changelog.
static brls::Dialog* makeUpdateDialog(const std::string& title, const std::string& body) {
    auto* box = dynamic_cast<brls::Box*>(brls::View::createFromXMLResource("view/update_dialog.xml"));
    if (auto* t = dynamic_cast<brls::Label*>(box->getView("update/title"))) t->setText(title);

    std::string notes = misc::markdownToText(body);
    size_t a = notes.find_first_not_of("\n\r \t");
    size_t z = notes.find_last_not_of("\n\r \t");
    notes = a == std::string::npos ? "" : notes.substr(a, z - a + 1);

    if (notes.empty())
        box->getView("update/scroll")->setVisibility(brls::Visibility::GONE);
    else if (auto* b = dynamic_cast<brls::Label*>(box->getView("update/body")))
        b->setText(notes);

    return new brls::Dialog(box);
}

void AppVersion::checkUpdate(int delay, bool showUpToDateDialog) {
    if (!AppVersion::updating->load()) {
        Dialog::cancelable("main/setting/others/updating"_i18n, [] { AppVersion::updating->store(true); });
        return;
    }
    ThreadPool::instance().submit([delay, showUpToDateDialog](HTTP& s) {
        // at startup, give priority to login requests
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        try {
            std::string url = fmt::format("https://api.github.com/repos/{}/releases/latest", git_repo);
            auto resp = HTTP::get(url, HTTP::Timeout{});
            nlohmann::json j = nlohmann::json::parse(resp);
            std::string latest_ver = j.at("tag_name").get<std::string>();
            if (!needUpdate(latest_ver)) {
                brls::Logger::info("App is up to date");
                if (showUpToDateDialog) brls::sync([]() { Dialog::show("main/setting/others/up2date"_i18n); });
                return;
            }

            // release notes shown in the popup (only this version's changelog)
            std::string release_body = j.value("body", std::string());

#ifdef __SWITCH__
            // NRO URL and size from the API response rather than a hardcoded
            // URL: robust to an asset rename, and the size lets us validate
            // the download before overwriting the app
            std::string asset_url;
            int64_t asset_size = 0;
            for (auto& asset : j.at("assets")) {
                std::string name = asset.at("name").get<std::string>();
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".nro") == 0) {
                    asset_url = asset.at("browser_download_url").get<std::string>();
                    asset_size = asset.value("size", int64_t(0));
                    break;
                }
            }
            if (asset_url.empty()) {
                brls::Logger::error("checkUpdate: no NRO asset in release {}", latest_ver);
                return;
            }

            brls::sync([latest_ver, asset_url, asset_size, release_body]() {
                std::string title = brls::getStr("main/setting/others/upgrade", latest_ver);
                auto dialog = makeUpdateDialog(title, release_body);
                dialog->addButton("hints/cancel"_i18n, []() {
                    auto& conf = AppConfig::instance();
                    conf.setItem(AppConfig::APP_UPDATE, getVersion());
                });
                dialog->addButton("hints/ok"_i18n,
                    [latest_ver, asset_url, asset_size]() { startUpdate(latest_ver, asset_url, asset_size); });
                dialog->open();
            });
#else
            brls::sync([latest_ver, release_body]() {
                std::string title = brls::getStr("main/setting/others/upgrade", latest_ver);
                auto dialog = makeUpdateDialog(title, release_body);
                dialog->addButton("hints/cancel"_i18n, []() {
                    auto& conf = AppConfig::instance();
                    conf.setItem(AppConfig::APP_UPDATE, getVersion());
                });
                std::string url = fmt::format("https://github.com/{}/releases/tag/{}", git_repo, latest_ver);
                dialog->addButton("hints/ok"_i18n, [url] { brls::Application::getPlatform()->openBrowser(url); });
                dialog->open();
            });
#endif
        } catch (const std::exception& ex) {
            brls::Logger::error("checkUpdate failed: {}", ex.what());
        }
    });
}
