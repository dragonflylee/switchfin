/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <borealis.hpp>
#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

/// Main sidebar: the static tabs (home, search, downloads, settings) come
/// from activity/main.xml, plus one tab per Plex library inserted after
/// the home tab once /library/sections answers. A server/profile switch
/// recreates the whole MainActivity, so the tabs follow the active server.
class MainTabFrame : public AutoTabFrame {
public:
    /// One reorderable sidebar entry surfaced to the libraries manager
    /// (see view/library_manager). `id` matches the AutoSidebarItem id
    /// ("lib/<sectionKey>", "tab/playlists", "tab/watchlist").
    struct SidebarEntry {
        std::string id;
        std::string label;  // section title (server) or i18n label (personal)
        std::string icon;   // @res svg path
        bool visible;
    };

    /// fetch the server libraries and insert their sidebar tabs
    void loadLibraries();

    /// remove the static tabs the active backend does not support
    /// (personal-list tab if ListKind::None, Playlists tab if !caps.playlists)
    void applyCapabilities();

    /// Reorderable entries (server libraries + Playlists + Watchlist) in their
    /// current display order, for the libraries manager.
    std::vector<SidebarEntry> getReorderableEntries();

    /// Persist (per active server) then apply live: reorder + show/hide of the
    /// reorderable block. `order` lists ALL reorderable ids; `hidden` the ones
    /// to hide.
    void setSidebarLayout(const std::vector<std::string>& order, const std::set<std::string>& hidden);

    static brls::View* create();

    /// frees the hidden tabs kept out of the view tree (see hiddenStash_)
    ~MainTabFrame() override;

private:
    void addLibraryTabs(const std::vector<plex::Section>& sections);
    /// offline mode: build the library tabs from the local catalog instead of
    /// /library/sections (SPEC §4.4)
    void addOfflineLibraryTabs();

    /// Rearranges the reorderable block (contiguous, right after Home) to match
    /// the saved per-server order + visibility. Safe to call live: it only
    /// moves/hides existing items, never recreates them.
    void applySidebarLayout();

    /// Reconciles the saved per-server layout with the tabs actually present:
    /// fills `order` with the reorderable ids (saved order first, new tabs
    /// appended in natural order) and `hidden` with the hidden ids (pruned to
    /// present ids).
    void computeLayout(std::vector<std::string>& order, std::set<std::string>& hidden);

    /// Natural (default) order of the reorderable ids actually present:
    /// libraries in server order, then Playlists, then Watchlist.
    std::vector<std::string> naturalOrder();

    std::vector<plex::Section> libs_;  // qualifying libraries (movie/show/photo)
    /// Hidden reorderable tabs are removed from the sidebar tree (NOT set
    /// Visibility::GONE — display:none breaks the yoga layout of the grow spacer
    /// when a tab is shown again) and kept alive here, keyed by tab id, so they
    /// can be re-inserted when shown. Owned: freed in ~MainTabFrame.
    std::map<std::string, AutoSidebarItem*> hiddenStash_;
    bool librariesLoaded = false;
};

class MainActivity : public brls::Activity {
public:
    // Declare that the content of this activity is the given XML file
    CONTENT_FROM_XML_RES("activity/main.xml");

    MainActivity();

    void onContentAvailable() override;

private:
    /// Adds the focusable avatar button pinned at the bottom of the sidebar; it
    /// opens the connection switcher. The ring takes the active backend accent.
    void addSidebarAvatar();

    /// Tucks small, dimmed battery + wifi indicators at the very bottom of the
    /// sidebar (under the gear) — moved here out of the footer.
    void addSidebarStatus();

    BRLS_BIND(MainTabFrame, tabFrame, "main/tabFrame");
    BRLS_BIND(brls::AppletFrame, frame, "main/frame");
};
