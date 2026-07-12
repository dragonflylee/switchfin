#include "activity/main_activity.hpp"
#include "view/connection_switcher.hpp"
#include "view/music_mini_bar.hpp"
#include "view/audio_player.hpp"
#include "view/music_now_playing.hpp"
#include "tab/media_collection.hpp"
#include "utils/image.hpp"
#include "utils/config.hpp"
#include "tab/offline_collection.hpp"
#include "utils/offline_library.hpp"
#include "utils/network_state.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include <borealis/views/bottom_bar.hpp>
#include <borealis/views/widgets/battery.hpp>
#include <borealis/views/widgets/wireless.hpp>
#include <borealis/views/dialog.hpp>

using namespace brls::literals;  // for _i18n

/// One-time "pleNx is now GMCA" welcome notice. Shown only to users whose data
/// dir was just relocated from a legacy pleNx/Switchlex build (never on a fresh
/// GMCA install), and only once (persistent RENAME_NOTICE_SHOWN flag). The flag
/// is set before opening so an abrupt close never re-triggers it.
static void maybeShowRenameNotice() {
    auto& conf = AppConfig::instance();
    if (!conf.migratedFromLegacy) return;
    if (conf.getItem(AppConfig::RENAME_NOTICE_SHOWN, false)) return;
    conf.setItem(AppConfig::RENAME_NOTICE_SHOWN, true);

    // Deferred to the next frame so the notice opens once MainActivity is fully
    // pushed and focused (opening a modal mid-onContentAvailable would steal
    // focus before the activity finishes wiring it).
    brls::sync([]() {
        auto* box = dynamic_cast<brls::Box*>(brls::View::createFromXMLResource("view/rename_notice.xml"));
        if (!box) return;
        auto* dialog = new brls::Dialog(box);
        dialog->addButton("main/rename/continue"_i18n, []() {});
        dialog->open();
    });
}

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

    // persistent "now playing" mini-bar under the tab frame (issue #11 T11):
    // stays visible across library/detail navigation while music plays
    if (brls::Box* column = dynamic_cast<brls::Box*>(this->tabFrame->getParent())) column->addView(new MusicMiniBar());

    // The mini-bar sits outside the tab frame's focus path (AutoTabFrame blocks
    // DOWN nav to it), so it is only pointer-tappable. Register a global Y action
    // on the frame so gamepad/keyboard users can open Now Playing whenever music
    // is playing (review finding). Returns false when idle so Y is untouched then;
    // a focused library grid keeps Y=sort (focused view wins the action).
    this->frame->registerAction(
        "main/music/now_playing"_i18n, brls::BUTTON_Y,
        [](brls::View*) {
            if (!AudioPlayer::instance().active()) return false;
            MusicNowPlaying::open();
            return true;
        },
        true);

    // rebrand welcome notice (gated: migrated-from-legacy users, once)
    maybeShowRenameNotice();
}

void MainActivity::addSidebarAvatar() {
    brls::Box* sidebar = this->tabFrame->getSidebar();
    brls::Box* footer = this->tabFrame->getSidebarFooter();
    if (!sidebar || !footer) return;

    // Move Settings out of the scrollable tab list into the pinned footer, so
    // the gear stays at the bottom next to the avatar, below the (possibly
    // scrolling) library tabs. Detach/re-attach keeps the tab (content +
    // creator) alive — same path as the reorder logic.
    auto* settings = dynamic_cast<AutoSidebarItem*>(sidebar->getView("tab/settings"));
    if (settings && settings->getParent() == sidebar) sidebar->removeView(settings, false);

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

    // Footer order, top to bottom: avatar, then the gear moved above, then the
    // network status row (addSidebarStatus). The scroll frame's grow keeps the
    // whole footer pinned to the bottom of the sidebar.
    this->tabFrame->addFooterTab(item, [] { return new ConnectionSwitcher(); });
    if (settings) footer->addView(settings);
}

void MainActivity::addSidebarStatus() {
    brls::Box* footer = this->tabFrame->getSidebarFooter();
    if (!footer) return;

    // wifi + battery, half the status-bar size and dimmed, in a row at the very
    // bottom of the pinned footer (appended after the gear). The widgets
    // gracefully collapse to nothing on platforms without battery/wireless info.
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

    footer->addView(status);
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
    // offline: no server to query — build the library tabs from the catalog
    if (NetworkState::isOffline()) {
        this->addOfflineLibraryTabs();
        return;
    }

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
    this->libs_.clear();

    // server order, right after the home tab
    size_t position = 1;
    for (auto& s : sections) {
        if (s.hidden) continue;
        // music (artist) is now supported (issue #11); other types stay out of scope
        if (s.type != plex::mediaTypeMovie && s.type != plex::mediaTypeShow && s.type != plex::mediaTypePhoto &&
            s.type != plex::mediaTypeArtist)
            continue;

        this->libs_.push_back(s);

        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        // stable id so the item can be targeted (reorder / hide) by the manager
        item->setId("lib/" + s.key);
        if (s.type == plex::mediaTypeMovie) {
            item->applyXMLAttribute("icon", "@res/icon/ico-movie.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-movie-activate.svg");
        } else if (s.type == plex::mediaTypeShow) {
            item->applyXMLAttribute("icon", "@res/icon/ico-tv.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-tv-activate.svg");
        } else if (s.type == plex::mediaTypeArtist) {
            item->applyXMLAttribute("icon", "@res/icon/ico-audio.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-audio.svg");
        } else {
            item->applyXMLAttribute("icon", "@res/icon/ico-media.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-media-activate.svg");
        }

        std::string key = s.key, type = s.type, title = s.title;
        // Backends that expose catalog sub-tabs (Stremio) get a catalogs-as-tabs
        // view; Plex/Jellyfin keep the standard MediaCollection.
        bool catalogTabs = !AppConfig::instance().backend().sectionTabs(key).empty();
        this->addTab(
            item,
            [key, type, title, catalogTabs]() -> brls::View* {
                if (catalogTabs) return new StremioCatalogs(key, type);
                return new MediaCollection(key, type, "", title);
            },
            position++);
    }

    // apply the saved per-server order + visibility (also groups Playlists /
    // Watchlist into the reorderable block, right after Home)
    this->applySidebarLayout();
}

std::vector<std::string> MainTabFrame::naturalOrder() {
    // libraries in server order, then the personal tabs that are present.
    // A tab is "present" whether it lives in the sidebar tree or is parked in
    // the hidden stash (hidden tabs are detached from the tree, see
    // applySidebarLayout), so check both.
    brls::Box* sb = this->getSidebar();
    auto present = [&](const std::string& id) {
        return sb->getView(id) != nullptr || this->hiddenStash_.count(id) > 0;
    };
    std::vector<std::string> ids;
    for (auto& s : this->libs_) ids.push_back("lib/" + s.key);
    if (present("tab/playlists")) ids.push_back("tab/playlists");
    if (present("tab/watchlist")) ids.push_back("tab/watchlist");
    return ids;
}

void MainTabFrame::computeLayout(std::vector<std::string>& order, std::set<std::string>& hidden) {
    order.clear();
    hidden.clear();

    std::vector<std::string> natural = this->naturalOrder();
    std::set<std::string> present(natural.begin(), natural.end());
    if (present.empty()) return;

    // read the saved layout for the active server
    std::vector<std::string> savedOrder;
    std::set<std::string> savedHidden;
    const std::string& sid = AppConfig::instance().getUser().server_id;
    if (!sid.empty()) {
        auto all = AppConfig::instance().getItem(AppConfig::SIDEBAR_LAYOUT, nlohmann::json::object());
        if (all.is_object() && all.contains(sid) && all.at(sid).is_object()) {
            auto& node = all.at(sid);
            if (node.contains("order") && node.at("order").is_array())
                for (auto& e : node.at("order"))
                    if (e.is_string()) savedOrder.push_back(e.get<std::string>());
            if (node.contains("hidden") && node.at("hidden").is_array())
                for (auto& e : node.at("hidden"))
                    if (e.is_string()) savedHidden.insert(e.get<std::string>());
        }
    }

    // saved ids first (present only, deduped), then new present ids in natural order
    std::set<std::string> placed;
    for (auto& id : savedOrder)
        if (present.count(id) && !placed.count(id)) {
            order.push_back(id);
            placed.insert(id);
        }
    for (auto& id : natural)
        if (!placed.count(id)) {
            order.push_back(id);
            placed.insert(id);
        }

    // keep only hidden ids that still exist
    for (auto& id : savedHidden)
        if (present.count(id)) hidden.insert(id);
}

void MainTabFrame::applySidebarLayout() {
    std::vector<std::string> order;
    std::set<std::string> hidden;
    this->computeLayout(order, hidden);
    if (order.empty()) return;

    brls::Box* sb = this->getSidebar();

    // Resolve every reorderable item, whether it is currently in the sidebar
    // tree or parked in the hidden stash. Hidden tabs are kept OUT of the tree
    // (rather than Visibility::GONE): toggling display:none on a sidebar item
    // corrupts the yoga layout of the grow spacer when the item is shown again
    // (the spacer renders as if it split the block). Detach/re-attach — the same
    // path used to build the sidebar — lays out cleanly.
    std::map<std::string, AutoSidebarItem*> items;
    for (auto& id : order) {
        auto* it = dynamic_cast<AutoSidebarItem*>(sb->getView(id));
        if (!it) {
            auto s = this->hiddenStash_.find(id);
            if (s != this->hiddenStash_.end()) it = s->second;
        }
        if (it) items[id] = it;
    }

    // detach every reorderable item currently in the tree (keep it alive: this
    // preserves its attached content + creator, so XML tabs survive too)
    for (auto& [id, it] : items)
        if (it->getParent() == sb) sb->removeView(it, false);

    // re-insert the visible ones as one contiguous block right after Home
    // (index 0); park the hidden ones in the stash (out of the tree)
    this->hiddenStash_.clear();
    size_t pos = 1;
    for (auto& id : order) {
        auto s = items.find(id);
        if (s == items.end()) continue;
        if (hidden.count(id))
            this->hiddenStash_[id] = s->second;
        else
            sb->addView(s->second, pos++);
    }
}

MainTabFrame::~MainTabFrame() {
    // stashed tabs are detached from the tree, so nothing else frees them
    for (auto& [id, it] : this->hiddenStash_) delete it;
}

void MainTabFrame::setSidebarLayout(const std::vector<std::string>& order, const std::set<std::string>& hidden) {
    const std::string& sid = AppConfig::instance().getUser().server_id;
    if (!sid.empty()) {
        auto all = AppConfig::instance().getItem(AppConfig::SIDEBAR_LAYOUT, nlohmann::json::object());
        if (!all.is_object()) all = nlohmann::json::object();
        nlohmann::json node;
        node["order"] = order;
        node["hidden"] = std::vector<std::string>(hidden.begin(), hidden.end());
        all[sid] = node;
        AppConfig::instance().setItem(AppConfig::SIDEBAR_LAYOUT, all);
    }
    this->applySidebarLayout();
}

std::vector<MainTabFrame::SidebarEntry> MainTabFrame::getReorderableEntries() {
    std::vector<std::string> order;
    std::set<std::string> hidden;
    this->computeLayout(order, hidden);

    auto& caps = AppConfig::instance().backend().caps();
    std::vector<SidebarEntry> out;
    out.reserve(order.size());
    for (auto& id : order) {
        SidebarEntry e;
        e.id = id;
        e.visible = hidden.count(id) == 0;
        if (id == "tab/playlists") {
            e.label = brls::getStr("main/playlist/title");
            e.icon = "@res/icon/ico-playlist.svg";
        } else if (id == "tab/watchlist") {
            e.label = caps.listKind == media::ListKind::Favorites ? brls::getStr("main/favorites/title")
                                                                  : brls::getStr("main/watchlist/title");
            e.icon = "@res/icon/ico-bookmark-fill.svg";
        } else {
            // "lib/<sectionKey>"
            std::string key = id.substr(4);
            const plex::Section* sec = nullptr;
            for (auto& s : this->libs_)
                if (s.key == key) {
                    sec = &s;
                    break;
                }
            e.label = sec ? sec->title : key;
            std::string type = sec ? sec->type : std::string();
            if (type == plex::mediaTypeMovie)
                e.icon = "@res/icon/ico-movie.svg";
            else if (type == plex::mediaTypeShow)
                e.icon = "@res/icon/ico-tv.svg";
            else if (type == plex::mediaTypeArtist)
                e.icon = "@res/icon/ico-audio.svg";
            else
                e.icon = "@res/icon/ico-media.svg";
        }
        out.push_back(e);
    }
    return out;
}

void MainTabFrame::addOfflineLibraryTabs() {
    if (this->librariesLoaded) return;
    this->librariesLoaded = true;

    size_t position = 1;
    for (auto& s : OfflineLibrary::instance().sections()) {
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        if (s.type == plex::mediaTypeShow) {
            item->applyXMLAttribute("icon", "@res/icon/ico-tv.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-tv-activate.svg");
        } else {
            item->applyXMLAttribute("icon", "@res/icon/ico-movie.svg");
            item->applyXMLAttribute("iconActivate", "@res/icon/ico-movie-activate.svg");
        }
        std::string key = s.key;
        this->addTab(item, [key]() { return new OfflineCollection(key); }, position++);
    }
}
