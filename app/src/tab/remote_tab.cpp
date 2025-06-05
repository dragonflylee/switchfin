#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

RemoteTab::RemoteTab() {
    this->inflateFromXMLRes("xml/tabs/remote.xml");
    brls::Logger::debug("RemoteTab: create");

    this->registerAction(
        "main/player/next"_i18n, brls::BUTTON_LB,
        [this](brls::View* view) {
            tabFrame->focus2LastTab();
            return true;
        },
        true);

    this->registerAction(
        "main/player/prev"_i18n, brls::BUTTON_RB,
        [this](brls::View* view) {
            tabFrame->focus2NextTab();
            return true;
        },
        true);
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
#if defined(USE_LIBUSBHSFS)
    if (conf.getItem(AppConfig::UMS, false)) {
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(22);
        item->setLabel("UMS");
        this->tabFrame->addTab(item, []() { return new UmsView(); });
    } else
#endif
        if (conf.getRemotes().empty()) {
        auto hintImage = new brls::Image();
        hintImage->setImageFromRes("img/empty.png");
        hintImage->setScalingType(brls::ImageScalingType::CENTER);
        this->tabFrame->setVisibility(brls::Visibility::GONE);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->addView(hintImage);
    }
}