#include <borealis.hpp>
#ifdef PS5_NATIVE_GPU
#include <borealis/platforms/desktop/desktop_platform.hpp>
#endif

#include "utils/config.hpp"
#include "utils/download.hpp"
#include "utils/thread.hpp"
#include "api/analytics.hpp"

#include "view/svg_image.hpp"
#include "view/custom_button.hpp"
#include "view/context_menu.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/recycling_grid.hpp"
#include "view/h_recycling.hpp"
#include "view/recyling_video.hpp"
#include "view/video_progress_slider.hpp"
#include "view/gallery_view.hpp"
#include "view/search_list.hpp"
#include "view/video_view.hpp"
#include "view/selector_cell.hpp"
#include "view/button_close.hpp"
#include "view/text_box.hpp"
#include "view/icon_button.hpp"
#include "view/mpv_core.hpp"

#include "activity/main_activity.hpp"
#include "activity/server_list.hpp"
#include "activity/hint_activity.hpp"
#include "activity/loading_activity.hpp"
#include "tab/home_tab.hpp"
#include "tab/media_folder.hpp"
#include "tab/search_tab.hpp"
#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "tab/setting_tab.hpp"

#if defined(__SDL2__)
#include <SDL2/SDL_main.h>
#endif

#ifdef PS5_NATIVE_GPU
#include <SDL2/SDL_filesystem.h>
#include <SDL2/SDL_stdinc.h>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <sys/stat.h>
#include <fcntl.h>
#include "utils/ps5_native_startup.hpp"
#include "utils/ps5_native_random.hpp"
#include "utils/ps5_native_netdb.hpp"
#include "utils/ps5_native_requests.hpp"
#include "utils/ps5_native_heap_failure.hpp"
#include "utils/ps5_storage_home.hpp"
#include <borealis/platforms/ps5/native_display.hpp>
#include <borealis/platforms/ps5/native_i18n.hpp>

#endif
using namespace brls::literals;  // for _i18n

#ifdef PS5_NATIVE_GPU
#define NATIVE_STARTUP_STAGE(name) ps5_native_startup::checkpoint(name)

static int runApplication(int argc, char* argv[]) {
    // Native resources are compiled as /app0/resources/ and writable paths
    // start at /download0. This sandbox rejects chdir("/app0") even while app0
    // is mounted; changing cwd must not gate initialization of absolute paths.
    NATIVE_STARTUP_STAGE("absolute-native-paths");
    // This native sandbox's settings are separate from every payload install.
    NATIVE_STARTUP_STAGE("settings-directory");
    mkdir("/download0/switchfin-native", 0700);
    // the unjail daemon un-chroots the whole process, so promotion is read
    // from the filesystem (the real sandbox root becomes visible) rather than
    // from the daemon's advisory reply. When promoted, every sandbox-absolute
    // path is rebased onto that real root BEFORE anything loads -- the asset
    // base here, configDir() (config/index/fonts/gamepad/subfont) and the CA
    // bundle below -- or the first hard filesystem access aborts startup.
    // downloadDir() then moves to /data when it probes writable. Without the
    // daemon nothing is promoted and every path stays in the sandbox.
    NATIVE_STARTUP_STAGE("storage-home");
    ps5::storage::resolve();
    std::string nativeAppRoot = "/app0/";
    if (ps5::storage::promoted()) {
        nativeAppRoot = ps5::storage::sandboxRoot() + "/app0/";
        brls::setResourceBase(nativeAppRoot + "resources/");
        mkdir((ps5::storage::sandboxRoot() + "/download0/switchfin-native").c_str(), 0700);
        NATIVE_STARTUP_STAGE("storage-home-promoted");
    }
    // The /download0 download journal cannot open once promoted, so record the
    // storage decision in the startup log, which keeps its pre-promotion fd.
    {
        const auto& storageReport = ps5::storage::state();
        ps5_native_startup::detail::line(
            "STORAGE unjail=%d probe=%d errno=%d elevated=%d promoted=%d\n",
            storageReport.unjail, storageReport.probeResult, storageReport.probeErrno,
            static_cast<int>(storageReport.elevated), static_cast<int>(ps5::storage::promoted()));
    }
    NATIVE_STARTUP_STAGE("trust-environment");
    // The CA bundle lives beside the app image, which moves with it once
    // promoted; curl reads these before the sandbox path would otherwise fail.
    const std::string nativeCaBundle = nativeAppRoot + "ca-bundle.crt";
    setenv("CURL_CA_BUNDLE", nativeCaBundle.c_str(), 1);
    setenv("SSL_CERT_FILE", nativeCaBundle.c_str(), 1);
    NATIVE_STARTUP_STAGE("sdl-main-ready");
    SDL_SetMainReady();

    NATIVE_STARTUP_STAGE("arguments");
#else
int main(int argc, char* argv[]) {
#endif
    std::vector<std::string> items;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        } else if (std::strcmp(argv[i], "-v") == 0) {
            brls::Application::enableDebuggingView(true);
        } else if (std::strcmp(argv[i], "-t") == 0) {
            MPVCore::DEBUG = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            const char* path = (i + 1 < argc) ? argv[++i] : "switchfin.log";
#ifdef PS5_NATIVE_GPU
            FILE* out = std::fopen(path, "w+");
            // Line-buffered, because the interesting case is a crash: the
            // handler ends in _exit(), which does not flush stdio, so a
            // block-buffered log loses everything since the last 4 KiB boundary
            // exactly when it is needed. It reliably arrived empty before this.
            if (out) {
                std::setvbuf(out, nullptr, _IOLBF, 0);
                brls::Logger::setLogOutput(out);
            } else {
                brls::Logger::warning("Could not open log output: {}", path);
            }
#else
            brls::Logger::setLogOutput(std::fopen(path, "w+"));
#endif
        } else if (std::strcmp(argv[i], "-version") == 0) {
            brls::Logger::info("{} {}", AppVersion::getDeviceName(), AppVersion::getCommit());
            return 0;
        } else {
            items.push_back(argv[i]);
        }
    }

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("application-log");
    // Once promoted, /download0 is dead; write logs under the real sandbox root
    // so application/driver output (including runtime errors like a failed
    // removal) is captured on a promoted run. Empty prefix = unchanged sandbox.
    const std::string nativeLogDir = ps5::storage::sandboxRoot() + "/download0/switchfin-native";
    const std::string appLogPath = nativeLogDir + "/application.log";
    const std::string driverLogPath = nativeLogDir + "/driver.log";
    const auto nativeLogBudget = brls::Ps5LogOutput::remainingForFile(appLogPath.c_str(), 8 * 1024 * 1024);
    if (nativeLogBudget) {
        if (FILE* out = std::fopen(appLogPath.c_str(), "a")) {
            std::setvbuf(out, nullptr, _IOLBF, 0);
            brls::Logger::setLogOutput(out);
            brls::Logger::setLogOutputLimit(nativeLogBudget);
        }
    }
    NATIVE_STARTUP_STAGE("driver-log");
    if (brls::Ps5LogOutput::remainingForFile(driverLogPath.c_str(), 2 * 1024 * 1024)) {
        const int fd = open(driverLogPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            if (dup2(fd, STDOUT_FILENO) >= 0) std::setvbuf(stdout, nullptr, _IONBF, 0);
            if (dup2(fd, STDERR_FILENO) >= 0) std::setvbuf(stderr, nullptr, _IONBF, 0);
            if (fd != STDOUT_FILENO && fd != STDERR_FILENO) close(fd);
        }
    }
    setenv("MPV_CLIENT_LOG_LEVEL", "info", 1);
    NATIVE_STARTUP_STAGE("first-logger-message");
    brls::Logger::info("Switchfin: configured {}x{} nominal {}Hz HDR output, software video decoding",
        brls::ps5_native_display::width,
        brls::ps5_native_display::height, brls::ps5_native_display::refreshHz);

    NATIVE_STARTUP_STAGE("setlocale");
#endif
    std::setlocale(LC_ALL, "C.UTF-8");
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("secure-random-init");
    errno = 0;
    if (!ps5_native_random::initialize()) {
        const int entropySystemError = errno;
        ps5_native_startup::failure_code(ps5_native_random::initialization_stage(),
            ps5_native_random::last_error());
        if (ps5_native_random::last_error() == -1)
            ps5_native_startup::failure_code("random-system-errno", entropySystemError);
        return 1;
    }
    NATIVE_STARTUP_STAGE("network-service-init");
    if (!ps5_native_netdb::initialize()) {
        ps5_native_startup::failure_code(ps5_native_netdb::initialization_stage(),
            ps5_native_netdb::last_error());
        return 1;
    }
#endif
    // Load cookies and settings
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("config-init");
#endif
    auto& conf = AppConfig::instance();
    if (!conf.init()) {
#ifdef PS5_NATIVE_GPU
        ps5_native_startup::failure("configuration initialization returned false");
        return 1;
#else
        return 0;
#endif
    }

    // Init the app and i18n
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("application-init");
#endif
    if (!brls::Application::init()) {
#ifdef PS5_NATIVE_GPU
        ps5_native_startup::failure("application initialization returned false");
#else
        brls::Logger::error("Unable to init application");
#endif
        return EXIT_FAILURE;
    }

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("themes-init");
#endif
    conf.initThemes();
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("downloads-init");
#endif
    DownloadManager::instance().init();

    // Return directly to the desktop when closing the application (only for NX)
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("platform-exit-mode");
#endif
    brls::Application::getPlatform()->exitToHomeMode(true);

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("create-window");
#endif
    brls::Application::createWindow(fmt::format("{} for {}", AppVersion::getPackageName(), AppVersion::getPlatform()));

    // Have the application register an action on every activity that will quit when you press BUTTON_START
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("register-views");
#endif
    brls::Application::setGlobalQuit(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("IconButton", IconButton::create);
    brls::Application::registerXMLView("MenuItem", MenuItem::create);
    brls::Application::registerXMLView("CustomButton", CustomButton::create);
    brls::Application::registerXMLView("SelectorCell", SelectorCell::create);
    brls::Application::registerXMLView("TextBox", TextBox::create);
    brls::Application::registerXMLView("ButtonClose", ButtonClose::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("HRecyclerFrame", HRecyclerFrame::create);
    brls::Application::registerXMLView("RecylingVideo", RecylingVideo::create);
    brls::Application::registerXMLView("GalleryView", GalleryView::create);
    brls::Application::registerXMLView("SearchList", SearchList::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("MediaFolders", MediaFolders::create);
    brls::Application::registerXMLView("SearchTab", SearchTab::create);
    brls::Application::registerXMLView("RemoteTab", RemoteTab::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("initial-activity");
    bool deferStartupUpdate = false;
#endif
    if (!brls::Application::getPlatform()->isApplicationMode()) {
        brls::Application::pushActivity(new HintActivity());
    } else if (items.size() > 0) {
        RemoteView::play(items.front());
    } else {
#ifdef PS5_NATIVE_GPU
        deferStartupUpdate = true;
#endif
        brls::Application::pushActivity(new LoadingActivity(), brls::TransitionAnimation::NONE);
        brls::Application::blockInputs();
#ifdef PS5_NATIVE_GPU
        NATIVE_STARTUP_STAGE("login-worker-schedule");
#endif
        brls::async([]() {
            const bool logged = AppConfig::instance().checkLogin();
            brls::sync([logged]() {
                brls::Application::unblockInputs();
                brls::Application::clear();
                if (!logged) {
                    brls::Application::pushActivity(new ServerList());
                } else {
                    brls::Application::pushActivity(new MainActivity());
                }
#ifdef PS5_NATIVE_GPU
                // Do not show the independent modal while startup owns input.
                AppVersion::checkUpdate();
#endif
            });
        });
    }

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("analytics-schedule");
#endif
    GA("open_app")

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("update-check-schedule");
    if (!deferStartupUpdate) AppVersion::checkUpdate();
#else
    AppVersion::checkUpdate();
#endif

    // Run the app
#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("first-main-loop");
    bool firstFrame = true;
    while (brls::Application::mainLoop()) {
        MPVCore::drainNativeCallbacks();
        ps5_native_requests::drain();
        if (firstFrame) {
            NATIVE_STARTUP_STAGE("main-loop-running");
            firstFrame = false;
        }
    }
#else
    while (brls::Application::mainLoop());
#endif

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("thread-pool-stop");
#endif
    ThreadPool::instance().stop();

#ifdef PS5_NATIVE_GPU
    NATIVE_STARTUP_STAGE("restart-check");
#endif
    conf.checkRestart(argv);
#ifdef PS5_NATIVE_GPU

#endif
    // Exit
    return EXIT_SUCCESS;
}
#ifdef PS5_NATIVE_GPU

int main(int argc, char* argv[]) {
    NATIVE_STARTUP_STAGE("main-entry");
    NATIVE_STARTUP_STAGE("native-services");
    NATIVE_STARTUP_STAGE("native-startup");
    ps5_native_startup::detail::line("HEAP-JOURNAL-INIT status=%d\n", ps5_native_heap_failure_initialize());
    int status = EXIT_FAILURE;
    try {
        status = runApplication(argc, argv);
    } catch (const std::filesystem::filesystem_error& error) {
        // Error codes and fixed operation labels remain useful when the
        // exception's private paths must not be written to the startup trace.
        ps5_native_startup::failure_error_code("filesystem exception", error.code());
        if (std::strcmp(ps5_native_startup::current_stage(), "application-init") == 0)
            ps5_native_startup::failure(brls::ps5_native_i18n::current_operation());
    } catch (const std::system_error& error) {
        ps5_native_startup::failure_error_code("system exception", error.code());
        if (std::strcmp(ps5_native_startup::current_stage(), "application-init") == 0)
            ps5_native_startup::failure(brls::ps5_native_i18n::current_operation());
    } catch (const std::exception& error) {
        // Preserve the failing stage even if Logger or its time formatting was
        // the original failure. Exception detail is bounded and redacted.
        ps5_native_startup::failure(error.what());
        if (std::strcmp(ps5_native_startup::current_stage(), "application-init") == 0)
            ps5_native_startup::failure(brls::ps5_native_i18n::current_operation());
    } catch (...) {
        ps5_native_startup::failure("unknown exception");
        if (std::strcmp(ps5_native_startup::current_stage(), "application-init") == 0)
            ps5_native_startup::failure(brls::ps5_native_i18n::current_operation());
    }

    ps5_native_startup::finish(status);
    // Retain the mounted sandbox after a controlled startup failure so its
    // error log can be inspected before the user closes the title.
    if (status != EXIT_SUCCESS) ps5_native_startup::hold_failure();
    // The native CRT returns through exit(status), which can fail on this runtime.
    // Native restart-required settings retain their UI for manual shell closure.
    // Full app-initiated quit remains an unqualified platform lifecycle contract.
    return status;
}
#endif
