#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

RemoteTab::RemoteTab() {
    this->inflateFromXMLRes("xml/tabs/remote.xml");
    brls::Logger::debug("RemoteTab: create");
    this->tabFrame->registerTabAction(this);
}

RemoteTab::~RemoteTab() { brls::Logger::debug("RemoteTab: deleted"); }

brls::View* RemoteTab::create() { return new RemoteTab(); }

void RemoteTab::onCreate() {
    auto& conf = AppConfig::instance();
    for (auto& r : conf.getRemotes()) {
        try {
            auto* item = new AutoSidebarItem();
            item->setTabStyle(AutoTabBarStyle::ACCENT);
            item->setFontSize(22);
            item->setLabel(r.name);

            auto c = remote::create(r);
            auto view = new RemoteView(c);
            this->tabFrame->addTab(item, [view, r]() {
                view->push(r.url);
                return view;
            });
        } catch (const std::exception& ex) {
            brls::Logger::warning("remote {} create {}", r.name, ex.what());
        }
    }

    auto* item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(22);
    item->setLabel("main/remote/local"_i18n);
    this->tabFrame->addTab(item, []() { return new UmsView(); });
}