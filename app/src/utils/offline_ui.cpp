#include <borealis.hpp>
#include "utils/offline_ui.hpp"
#include "utils/config.hpp"
#include "utils/network_state.hpp"
#include "activity/main_activity.hpp"

using namespace brls::literals;

void offline_ui::tryReconnect() {
    brls::Application::notify("main/download/reconnecting"_i18n);
    // checkLogin probes the remembered URLs (2 s each) — must run off the UI thread
    brls::async([]() {
        bool ok = AppConfig::instance().checkLogin();
        brls::sync([ok]() {
            if (ok) {
                NetworkState::setOffline(false);
                brls::Application::clear();
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
            } else {
                brls::Application::notify("main/download/still_offline"_i18n);
            }
        });
    });
}

brls::View* offline_ui::makeEmpty() {
    brls::View* view = brls::View::createFromXMLResource("view/offline_empty.xml");
    if (brls::View* retry = view->getView("offline/empty/retry")) {
        retry->registerClickAction([](brls::View*) {
            offline_ui::tryReconnect();
            return true;
        });
    }
    return view;
}
