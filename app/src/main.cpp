#include <borealis.hpp>

#include "utils/config.hpp"
#include "utils/thread.hpp"

#include "view/svg_image.hpp"
#include "view/custom_button.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/recycling_grid.hpp"
#include "view/h_recycling.hpp"
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
#include "tab/server_add.hpp"
#include "tab/home_tab.hpp"
#include "tab/media_folder.hpp"
#include "tab/search_tab.hpp"
#include "tab/remote_tab.hpp"
#include "tab/setting_tab.hpp"

using namespace brls::literals;  // for _i18n

int main(int argc, char* argv[]) {
    std::string itemId;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        } else if (std::strcmp(argv[i], "-v") == 0) {
            brls::Application::enableDebuggingView(true);
        } else if (std::strcmp(argv[i], "-t") == 0) {
            MPVCore::DEBUG = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            const char* path = (i + 1 < argc) ? argv[++i] : "switchfin.log";
            brls::Logger::setLogOutput(std::fopen(path, "w+"));
        } else if (std::strcmp(argv[i], "-i") == 0) {
            if (i + 1 < argc) itemId = argv[++i];
        }
    }

    // Load cookies and settings
    if (!AppConfig::instance().init()) {
        return 0;
    }

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init application");
        return EXIT_FAILURE;
    }

    // Return directly to the desktop when closing the application (only for NX)
    brls::Application::getPlatform()->exitToHomeMode(true);

    brls::Application::createWindow(fmt::format("{} for {}", AppVersion::getPackageName(), AppVersion::getPlatform()));

    // Have the application register an action on every activity that will quit when you press BUTTON_START
    brls::Application::setGlobalQuit(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("CustomButton", CustomButton::create);
    brls::Application::registerXMLView("SelectorCell", SelectorCell::create);
    brls::Application::registerXMLView("TextBox", TextBox::create);
    brls::Application::registerXMLView("ButtonClose", ButtonClose::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("HRecyclerFrame", HRecyclerFrame::create);
    brls::Application::registerXMLView("GalleryView", GalleryView::create);
    brls::Application::registerXMLView("SearchList", SearchList::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("MediaFolders", MediaFolders::create);
    brls::Application::registerXMLView("SearchTab", SearchTab::create);
    brls::Application::registerXMLView("RemoteTab", RemoteTab::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);

    brls::Theme::getLightTheme().addColor("color/app", nvgRGB(2, 176, 183));
    brls::Theme::getDarkTheme().addColor("color/app", nvgRGB(51, 186, 227));
    // 用于骨架屏背景色
    brls::Theme::getLightTheme().addColor("color/grey_1", nvgRGB(245, 246, 247));
    brls::Theme::getDarkTheme().addColor("color/grey_1", nvgRGB(51, 52, 53));
    brls::Theme::getLightTheme().addColor("color/grey_2", nvgRGB(245, 245, 245));
    brls::Theme::getDarkTheme().addColor("color/grey_2", nvgRGB(51, 53, 55));
    brls::Theme::getLightTheme().addColor("color/grey_3", nvgRGBA(200, 200, 200, 16));
    brls::Theme::getDarkTheme().addColor("color/grey_3", nvgRGBA(160, 160, 160, 160));
    brls::Theme::getLightTheme().addColor("color/pink_1", nvgRGB(252, 237, 241));
    brls::Theme::getDarkTheme().addColor("color/pink_1", nvgRGB(44, 27, 34));
    // 分割线颜色
    brls::Theme::getLightTheme().addColor("color/line", nvgRGB(208, 208, 208));
    brls::Theme::getDarkTheme().addColor("color/line", nvgRGB(100, 100, 100));
    // 深浅配色通用的灰色字体颜色
    brls::Theme::getLightTheme().addColor("font/grey", nvgRGB(148, 153, 160));
    brls::Theme::getDarkTheme().addColor("font/grey", nvgRGB(148, 153, 160));

    brls::getStyle().addMetric("main/content_padding_sides", 30.f);
    brls::getStyle().addMetric("main/content_padding_top_bottom", 35.f);

    if (!brls::Application::getPlatform()->isApplicationMode()) {
        brls::Application::pushActivity(new HintActivity());
    } else if (!AppConfig::instance().checkLogin()) {
        brls::Application::pushActivity(new ServerList());
    } else if (itemId.empty()) {
        brls::Application::pushActivity(new MainActivity());
    } else {
        //brls::View* view = new VideoView(itemId);
        //brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    AppVersion::checkUpdate();
    // Run the app
    while (brls::Application::mainLoop())
        ;

    ThreadPool::instance().stop();

    AppConfig::instance().checkRestart(argv);
    // Exit
    return EXIT_SUCCESS;
}
