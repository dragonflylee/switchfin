#include <borealis.hpp>

#include "utils/config.hpp"
#include "utils/download.hpp"
#include "utils/thread.hpp"

#include "view/svg_image.hpp"
#include "view/icon_button.hpp"
#include "view/context_menu.hpp"
#include "view/custom_button.hpp"
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
#include "view/mpv_core.hpp"

#include "activity/main_activity.hpp"
#include "activity/server_list.hpp"
#include "activity/hint_activity.hpp"
#include "activity/loading_activity.hpp"
#include "tab/server_add.hpp"
#include "tab/home_tab.hpp"
#include "tab/search_tab.hpp"
#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "tab/setting_tab.hpp"
#include "tab/playlists_tab.hpp"
#include "tab/watchlist_tab.hpp"

#if defined(__SDL2__)
#include <SDL2/SDL_main.h>
#endif

using namespace brls::literals;  // for _i18n

#if defined(__SWITCH__) && defined(BUILTIN_NSP)
#include <switch.h>

/// La tuile HOME (forwarder NSP, title id FORWARDER_TITLEID = PROJECT_TITLEID
/// de CMakeLists.txt) est-elle déjà installée ? Best effort : si le service ns
/// échoue on répond « non » (la proposition n'est de toute façon affichée
/// qu'une seule fois, cf. AppConfig::HINT_FORWARDER).
static bool isForwarderInstalled() {
    if (R_FAILED(nsInitialize())) return false;
    bool found = false;
    NsApplicationRecord record;
    s32 count = 0;
    for (s32 offset = 0; R_SUCCEEDED(nsListApplicationRecord(&record, 1, offset, &count)) && count > 0; offset++) {
        if (record.application_id == FORWARDER_TITLEID) {
            found = true;
            break;
        }
    }
    nsExit();
    return found;
}

/// Premier lancement en mode application : propose d'installer la tuile HOME
/// (lancée depuis la tuile elle-même ou tuile déjà posée → ns la voit → rien).
static void proposeForwarderInstall() {
    auto& conf = AppConfig::instance();
    if (conf.getItem(AppConfig::HINT_FORWARDER, false)) return;
    conf.setItem(AppConfig::HINT_FORWARDER, true);
    if (isForwarderInstalled()) return;

    auto dialog = new brls::Dialog("main/hints/prompt"_i18n);
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("hints/ok"_i18n, []() { brls::Application::pushActivity(new HintActivity()); });
    dialog->open();
}
#endif

int main(int argc, char* argv[]) {
    std::vector<std::string> items;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        } else if (std::strcmp(argv[i], "-v") == 0) {
            brls::Application::enableDebuggingView(true);
        } else if (std::strcmp(argv[i], "-t") == 0) {
            MPVCore::DEBUG = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            const char* path = (i + 1 < argc) ? argv[++i] : "plenx.log";
            FILE* logFile = std::fopen(path, "w+");
            // line-buffered : sans ça les derniers ~16 Ko de logs (dont la
            // ligne qui précède un crash) restent dans le buffer et meurent
            // avec le process — invivable pour diagnostiquer
            if (logFile) std::setvbuf(logFile, nullptr, _IOLBF, 0);
            brls::Logger::setLogOutput(logFile);
        } else if (std::strcmp(argv[i], "-version") == 0) {
            brls::Logger::info("{} {}", AppVersion::getDeviceName(), AppVersion::getCommit());
            return 0;
        } else {
            items.push_back(argv[i]);
        }
    }

    std::setlocale(LC_ALL, "C.UTF-8");
    // Load cookies and settings
    auto& conf = AppConfig::instance();
    if (!conf.init()) {
        return 0;
    }

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init application");
        return EXIT_FAILURE;
    }

    conf.initThemes();
    DownloadManager::instance().init();

    // Return directly to the desktop when closing the application (only for NX)
    brls::Application::getPlatform()->exitToHomeMode(true);

    brls::Application::createWindow(fmt::format("{} for {}", AppVersion::getPackageName(), AppVersion::getPlatform()));

    // Have the application register an action on every activity that will quit when you press BUTTON_START
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
    brls::Application::registerXMLView("MainTabFrame", MainTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("HRecyclerFrame", HRecyclerFrame::create);
    brls::Application::registerXMLView("RecylingVideo", RecylingVideo::create);
    brls::Application::registerXMLView("GalleryView", GalleryView::create);
    brls::Application::registerXMLView("SearchList", SearchList::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("SearchTab", SearchTab::create);
    brls::Application::registerXMLView("RemoteTab", RemoteTab::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);
    brls::Application::registerXMLView("PlaylistsTab", PlaylistsTab::create);
    brls::Application::registerXMLView("WatchlistTab", WatchlistTab::create);

    if (!brls::Application::getPlatform()->isApplicationMode()) {
        brls::Application::pushActivity(new HintActivity());
    } else if (items.size() > 0) {
        RemoteView::play(items.front());
    } else {
        // checkLogin() sonde en série les URL mémorisées du serveur actif
        // (timeout 2 s par URL, config.cpp:checkLogin) : appelé ici sur le
        // thread principal il gelait la toute première frame plusieurs
        // secondes (réseau changé, serveur éteint…). On affiche l'écran de
        // chargement et on sonde en arrière-plan (recette n°6).
        brls::Application::pushActivity(new LoadingActivity(), brls::TransitionAnimation::NONE);
        brls::Application::blockInputs();
        brls::async([]() {
            const bool logged = AppConfig::instance().checkLogin();
            brls::sync([logged]() {
                brls::Application::unblockInputs();
                brls::Application::clear();
                if (logged) {
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
#if defined(__SWITCH__) && defined(BUILTIN_NSP)
                    proposeForwarderInstall();
#endif
                } else {
                    brls::Application::pushActivity(new ServerList(), brls::TransitionAnimation::NONE);
                }
            });
        });
    }

    std::string v = conf.getItem(AppConfig::APP_UPDATE, std::string("NaN"));
    if (AppVersion::getVersion().compare(v)) AppVersion::checkUpdate();

    // Run the app
    while (brls::Application::mainLoop());

    ThreadPool::instance().stop();

    conf.checkRestart(argv);
    // Exit
    return EXIT_SUCCESS;
}
