#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

RemoteTab::RemoteTab() {
    this->inflateFromXMLRes("xml/tabs/remote.xml");
    brls::Logger::debug("RemoteTab: create");

    this->registerAction(
        "上一项", brls::ControllerButton::BUTTON_LB,
        [this](brls::View* view) -> bool {
            tabFrame->focus2LastTab();
            return true;
        },
        true);

    this->registerAction(
        "下一项", brls::ControllerButton::BUTTON_RB,
        [this](brls::View* view) -> bool {
            tabFrame->focus2NextTab();
            return true;
        },
        true);
}

RemoteTab::~RemoteTab() { brls::Logger::debug("RemoteTab: deleted"); }

brls::View* RemoteTab::create() { return new RemoteTab(); }

void RemoteTab::onCreate() {
    for (auto& r : AppConfig::instance().getRemotes()) {
        auto c = remote::create(r);
        if (!c) continue;
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::PLAIN);
        item->setLabel(r.name);
        this->tabFrame->addTab(item, [c]() {
            auto view = new RemoteView(c);
            view->push(c->rootPath());
            return view;
        });
    }
}