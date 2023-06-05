// Switch include only necessary for demo videos recording
#ifdef __SWITCH__
#include <switch.h>
#endif

#include <borealis.hpp>

#include "utils/config.hpp"
#include "utils/thread.hpp"

#include "view/svg_image.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_progress_slider.hpp"

#include "activity/main_activity.hpp"
#include "activity/server_list.hpp"
#include "tab/server_add.hpp"
#include "tab/home_tab.hpp"
#include "tab/media_folder.hpp"
#include "tab/setting_tab.hpp"

using namespace brls::literals;  // for _i18n

int main(int argc, char* argv[]) {
    // Enable recording for Twitter memes
#ifdef __SWITCH__
    appletInitializeGamePlayRecording();
#endif

    // Set log level
    // We recommend to use INFO for real apps
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

    // Load cookies and settings
    AppConfig::instance().init();

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow(AppVersion::getPlatform());

    // Have the application register an action on every activity that will quit when you press BUTTON_START
    brls::Application::setGlobalQuit(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("VideoProgressSlider", VideoProgressSlider::create);
    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("MediaFolders", MediaFolders::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);

    brls::Theme::getLightTheme().addColor("color/app", nvgRGB(2, 176, 183));
    brls::Theme::getDarkTheme().addColor("color/app", nvgRGB(51, 186, 227));
    // 用于骨架屏背景色
    brls::Theme::getLightTheme().addColor("color/grey_1", nvgRGB(245, 246, 247));
    brls::Theme::getDarkTheme().addColor("color/grey_1", nvgRGB(51, 52, 53));
    brls::Theme::getLightTheme().addColor("color/grey_3", nvgRGBA(200, 200, 200, 16));
    brls::Theme::getDarkTheme().addColor("color/grey_3", nvgRGBA(160, 160, 160, 160));
    // 分割线颜色
    brls::Theme::getLightTheme().addColor("color/line", nvgRGB(208, 208, 208));
    brls::Theme::getDarkTheme().addColor("color/line", nvgRGB(208, 208, 208));
    // 深浅配色通用的灰色字体颜色
    brls::Theme::getLightTheme().addColor("font/grey", nvgRGB(148, 153, 160));
    brls::Theme::getDarkTheme().addColor("font/grey", nvgRGB(148, 153, 160));

    if (AppConfig::instance().getUser().access_token.empty()) {
        brls::Application::pushActivity(new ServerList());
    } else {
        // Create and push the main activity to the stack
        brls::Application::pushActivity(new MainActivity());
    }

    AppVersion::checkUpdate();
    // Run the app
    while (brls::Application::mainLoop())
        ;

    ThreadPool::instance().stop();
    // Exit
    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
