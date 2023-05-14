// Switch include only necessary for demo videos recording
#ifdef __SWITCH__
#include <switch.h>
#endif

#include <borealis.hpp>
#include <cstdlib>
#include <string>

#include "utils/config.hpp"

#include "activity/main_activity.hpp"
#include "view/svg_image.hpp"
#include "view/auto_tab_frame.hpp"
#include "tab/home_tab.hpp"
#include "tab/setting_tab.hpp"

using namespace brls::literals;  // for _i18n

int main(int argc, char* argv[]) {
    // Enable recording for Twitter memes
#ifdef __SWITCH__
    appletInitializeGamePlayRecording();
#endif

    // Set log level
    // We recommend to use INFO for real apps
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    // Load cookies and settings
    AppConfig::instance().init();

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("Jellyfin");

    // Have the application register an action on every activity that will quit when you press BUTTON_START
    brls::Application::setGlobalQuit(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("SettingTab", SettingTab::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);

    brls::Theme::getLightTheme().addColor("color/grey_1", nvgRGB(245, 246, 247));
    brls::Theme::getDarkTheme().addColor("color/grey_1", nvgRGB(51, 52, 53));
    brls::Theme::getLightTheme().addColor("color/jellyfin", nvgRGB(2, 176, 183));
    brls::Theme::getDarkTheme().addColor("color/jellyfin", nvgRGB(51, 186, 227));
    // 用于骨架屏背景色
    brls::Theme::getLightTheme().addColor("color/grey_3", nvgRGBA(200, 200, 200, 16));
    brls::Theme::getDarkTheme().addColor("color/grey_3", nvgRGBA(160, 160, 160, 160));

    // Add custom values to the style
    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    // 深浅配色通用的灰色字体颜色
    brls::Theme::getLightTheme().addColor("font/grey", nvgRGB(148, 153, 160));
    brls::Theme::getDarkTheme().addColor("font/grey", nvgRGB(148, 153, 160));

    // Create and push the main activity to the stack
    brls::Application::pushActivity(new MainActivity());

    // Run the app
    while (brls::Application::mainLoop())
        ;

    // Exit
    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
