#include "activity/main_activity.hpp"
#include "tab/media_collection.hpp"
#include "api/plex.hpp"

MainActivity::MainActivity() { brls::Logger::debug("MainActivity: create"); }

void MainActivity::onContentAvailable() { this->tabFrame->loadLibraries(); }

brls::View* MainTabFrame::create() { return new MainTabFrame(); }

void MainTabFrame::loadLibraries() {
    ASYNC_RETAIN
    // GET /library/sections -> Directory[]
    plex::getJSON<plex::Container<plex::Section>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Section>& r) {
            ASYNC_RELEASE
            this->addLibraryTabs(r.Items);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            // network failure: the static sidebar stays fully usable
            brls::Logger::warning("MainTabFrame: sections {}", ex);
        },
        plex::apiSections);
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
        this->addTab(item, [key, type]() { return new MediaCollection(key, type); }, position++);
    }
}
