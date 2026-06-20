#include "activity/main_activity.hpp"
#include "view/connection_switcher.hpp"
#include "tab/media_collection.hpp"
#include "utils/image.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include <borealis/views/bottom_bar.hpp>
#include <borealis/views/widgets/battery.hpp>
#include <borealis/views/widgets/wireless.hpp>

MainActivity::MainActivity() { brls::Logger::debug("MainActivity: create"); }

void MainActivity::onContentAvailable() {
    this->tabFrame->loadLibraries();
    this->tabFrame->applyCapabilities();

    // The footer is now button hints only, floating bottom-right over the
    // content (no band, no separator). Inset its gradient scrim by the sidebar
    // width so the fade covers the content area only, never the sidebar.
    if (auto* bar = dynamic_cast<brls::BottomBar*>(this->frame->getFooter()))
        bar->setContentInsetLeft(this->tabFrame->getSidebarWidth());

    this->addSidebarAvatar();
    this->addSidebarStatus();
}

void MainActivity::addSidebarAvatar() {
    brls::Box* sidebar = this->tabFrame->getSidebar();
    if (!sidebar || sidebar->getChildren().empty()) return;

    // The avatar is a REAL sidebar tab whose content is the connection switcher:
    // the tab group then handles its active state (focusing it deactivates the
    // other tabs, so no stale "active" item lingers), and it gets the exact focus
    // look of the other items (translucent bg on focus, accent bar when active).
    auto* item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);  // inflates the item template

    // Show the active profile's avatar instead of an SVG icon: the SVG icon stays
    // GONE (no "icon" attribute), we drop a round avatar into the icon box.
    if (auto* iconBox = dynamic_cast<brls::Box*>(item->getView("autoSidebar/item_label_box"))) {
        // hide the empty (no-text) label + subtitle so they don't reserve space
        // above the avatar (which would push it off-center, down/right).
        if (auto* lbl = item->getView("autoSidebar/item_label")) lbl->setVisibility(brls::Visibility::GONE);
        if (auto* sub = item->getView("autoSidebar/subtitle_label")) sub->setVisibility(brls::Visibility::GONE);

        NVGcolor accent = brls::Application::getTheme().getColor("color/app");
        auto* ring = new brls::Box();
        ring->setWidth(36);
        ring->setHeight(36);
        ring->setCornerRadius(18);
        ring->setBorderThickness(2.f);
        ring->setBorderColor(accent);
        // center in the icon box (the dynamically-added child does not inherit
        // the box's alignItems:center, so it would otherwise left-align and sit
        // off-center to the right).
        ring->setAlignSelf(brls::AlignSelf::CENTER);
        ring->setAlignItems(brls::AlignItems::CENTER);
        ring->setJustifyContent(brls::JustifyContent::CENTER);
        auto* img = new brls::Image();
        img->setWidth(32);
        img->setHeight(32);
        img->setCornerRadius(16);
        img->setScalingType(brls::ImageScalingType::FILL);
        img->setImageFromRes("img/account.png");
        const AppUser& u = AppConfig::instance().getUser();
        if (!u.thumb.empty()) Image::with(img, u.thumb);
        ring->addView(img);
        iconBox->addView(ring);
    }

    // Settings is the last sidebar item at this point (library tabs load async,
    // inserted right after Home). A grow spacer + the avatar tab go just before
    // it, so the order ends with ...tabs, spacer, avatar, settings.
    size_t settingsIdx = sidebar->getChildren().size() - 1;
    auto* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    sidebar->addView(spacer, settingsIdx);
    this->tabFrame->addTab(item, [] { return new ConnectionSwitcher(); }, settingsIdx + 1);
}

void MainActivity::addSidebarStatus() {
    brls::Box* sidebar = this->tabFrame->getSidebar();
    if (!sidebar) return;

    // wifi + battery, half the status-bar size and dimmed, in a row at the very
    // bottom of the sidebar (appended after the gear). The widgets gracefully
    // collapse to nothing on platforms without battery/wireless info.
    auto* status = new brls::Box();
    status->setAxis(brls::Axis::ROW);
    status->setJustifyContent(brls::JustifyContent::CENTER);
    status->setAlignItems(brls::AlignItems::CENTER);
    status->setMarginTop(12);
    status->setMarginBottom(2);
    status->setAlpha(0.6f);

    auto* wifi = new brls::WirelessWidget(0.5f);
    wifi->setMarginRight(2);
    status->addView(wifi);
    status->addView(new brls::BatteryWidget(0.5f));

    sidebar->addView(status);
}

void MainTabFrame::applyCapabilities() {
    auto& caps = AppConfig::instance().backend().caps();
    // personal-list tab: shown as Watchlist (Plex) or Favorites (Jellyfin/Emby);
    // removed entirely when the backend has neither
    if (caps.listKind == media::ListKind::None) this->removeTabById("tab/watchlist");
    if (!caps.playlists) this->removeTabById("tab/playlists");
}

brls::View* MainTabFrame::create() { return new MainTabFrame(); }

void MainTabFrame::loadLibraries() {
    ASYNC_RETAIN
    // libraries / catalogs -> sidebar tabs
    AppConfig::instance().backend().listSections(
        [ASYNC_TOKEN](const media::Container<media::Section>& r) {
            ASYNC_RELEASE
            this->addLibraryTabs(r.Items);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            // network failure: the static sidebar stays fully usable
            brls::Logger::warning("MainTabFrame: sections {}", ex);
        });
}

void MainTabFrame::addLibraryTabs(const std::vector<plex::Section>& sections) {
    // tabs are only built once per activity: ignore duplicate deliveries
    if (this->librariesLoaded) return;
    this->librariesLoaded = true;

    // server order, right after the home tab
    size_t position = 1;
    for (auto& s : sections) {
        if (s.hidden) continue;
        // music and other types are out of scope (PLEX_MIGRATION.md D2/D4)
        if (s.type != plex::mediaTypeMovie && s.type != plex::mediaTypeShow && s.type != plex::mediaTypePhoto)
            continue;

        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        if (s.type == plex::mediaTypeMovie) {
            item->applyXMLAttribute("icon", "@res/icon/ico-movie.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-movie-activate.svg");
        } else if (s.type == plex::mediaTypeShow) {
            item->applyXMLAttribute("icon", "@res/icon/ico-tv.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-tv-activate.svg");
        } else {
            item->applyXMLAttribute("icon", "@res/icon/ico-media.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-media-activate.svg");
        }

        std::string key = s.key, type = s.type;
        // Backends that expose catalog sub-tabs (Stremio) get a catalogs-as-tabs
        // view; Plex/Jellyfin keep the standard MediaCollection.
        bool catalogTabs = !AppConfig::instance().backend().sectionTabs(key).empty();
        this->addTab(
            item,
            [key, type, catalogTabs]() -> brls::View* {
                if (catalogTabs) return new StremioCatalogs(key, type);
                return new MediaCollection(key, type);
            },
            position++);
    }
}
