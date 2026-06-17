#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "tab/remote_add.hpp"
#include "tab/download_tab.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

RemoteTab::RemoteTab() {
    this->inflateFromXMLRes("xml/tabs/remote.xml");
    brls::Logger::debug("RemoteTab: create");
    this->tabFrame->registerTabAction(this);

    auto addAction = [this](...) {
        RemoteAdd::open([this]() { this->refresh(); });
        return true;
    };
    this->registerAction("main/remote/add"_i18n, brls::BUTTON_X, addAction);
    this->registerAction(brls::BRLS_KBD_KEY_INSERT, addAction);
}

RemoteTab::~RemoteTab() { brls::Logger::debug("RemoteTab: deleted"); }

brls::View* RemoteTab::create() { return new RemoteTab(); }

void RemoteTab::onCreate() {
    AutoSidebarItem* item;
    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(22);
    item->setLabel("main/tabs/downloads"_i18n);
    this->tabFrame->addTab(item, []() { return new DownloadView(); });

    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(22);
    item->setLabel("main/remote/local"_i18n);
    this->tabFrame->addTab(item, []() { return new UmsView(); });

    auto& remotes = AppConfig::instance().getRemotes();
    for (size_t i = 0; i < remotes.size(); i++) {
        auto& r = remotes[i];

        try {
            item = new AutoSidebarItem();
            item->setTabStyle(AutoTabBarStyle::ACCENT);
            item->setFontSize(22);
            item->setLabel(r.name);

            auto c = remote::create(r);
            auto view = new RemoteView(c);
            view->registerAction("hints/edit"_i18n, brls::BUTTON_Y, [this, i](...) {
                RemoteAdd::open([this]() { this->refresh(); }, (int)i);
                return true;
            });
            this->tabFrame->addTab(item, [view, r]() {
                view->push(r.url);
                return view;
            });
        } catch (const std::exception& ex) {
            brls::Logger::warning("remote {} create {}", r.name, ex.what());
        }
    }
}

void RemoteTab::refresh() {
    // the focus may be in a view about to be destroyed: first put it
    // back on the parent frame's sidebar (which survives)
    AutoTabFrame::focus2Sidebar(this);
    this->tabFrame->clearTabs();
    this->onCreate();
}