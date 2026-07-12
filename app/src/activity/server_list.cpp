/*
    Copyright 2023 dragonflylee
*/

#include "activity/server_list.hpp"
#include "view/connection_switcher.hpp"
#include "tab/setting_tab.hpp"
#include "tab/remote_tab.hpp"
#include <optional>

using namespace brls::literals;  // for _i18n

ServerList::ServerList() {
    brls::Logger::debug("ServerList: create");
    // logged-out root: neutral pleNx DEFAULT accent (each tile carries its own
    // brand tint). When connected, the switcher is shown as a detail over the
    // app instead, keeping the backend accent.
    AppConfig::instance().applyTheme(std::nullopt);
}

ServerList::~ServerList() { brls::Logger::debug("ServerList Activity: delete"); }

void ServerList::onContentAvailable() {
    // the footer is button hints only now, floating over the content; it
    // self-configures (gradient/pill) in its constructor — nothing to set here.
    auto* switcher = new ConnectionSwitcher();
    this->content->addView(switcher);
    brls::Application::giveFocus(switcher->getDefaultFocus());

    // Logged-out root: keep Settings + Remote reachable (a connected app exposes
    // them in its own sidebar). Footer actions, only when there is no connection.
    if (AppConfig::instance().getUserId().empty()) {
        this->frame->registerAction("main/tabs/setting"_i18n, brls::BUTTON_Y, [](brls::View* view) {
            auto* tab = new SettingTab();
            tab->onCreate();
            view->present(tab);
            return true;
        });
        this->frame->registerAction("main/tabs/remote"_i18n, brls::BUTTON_RB, [](brls::View* view) {
            auto* tab = new RemoteTab();
            tab->onCreate();
            view->present(tab);
            return true;
        });
    }
}
