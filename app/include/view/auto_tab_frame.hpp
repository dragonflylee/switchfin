/*
    Copyright 2022 xfangfang
    Copyright 2019-2021 natinusala
    Copyright 2019 WerWolv
    Copyright 2019 p-sam

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

typedef std::function<brls::View*(void)> TabViewCreator;

enum class AutoTabBarPosition { TOP, LEFT, RIGHT };

enum class AutoTabBarStyle { ACCENT, PLAIN, NONE };

class AutoSidebarItemGroup;
class AttachedView;
class SVGImage;

class AutoSidebarItem : public brls::Box {
public:
    AutoSidebarItem();

    void onFocusGained() override;
    void onFocusLost() override;

    void setGroup(AutoSidebarItemGroup* group);

    void setLabel(std::string label);

    void setSubtitle(std::string label);

    std::string getLabel();

    void setActive(bool active);

    bool isActive();

    void setFontSize(float size);

    void setHorizontalMode(bool value);

    bool getHorizontalMode();

    size_t getCurrentIndex();

    View* getAttachedView();

    View* createAttachedView();

    View* getView(std::string id) override;

    void setAttachedViewCreator(TabViewCreator creator);

    ~AutoSidebarItem() override;

    brls::GenericEvent* getActiveEvent();

    static AutoTabBarStyle getTabStyle(std::string value);

    void setTabStyle(AutoTabBarStyle style);

    void setDefaultBackgroundColor(NVGcolor c);

    void setActiveBackgroundColor(NVGcolor c);

    void setActiveTextColor(NVGcolor c);

private:
    BRLS_BIND(brls::Rectangle, accent, "autoSidebar/item_accent");
    BRLS_BIND(brls::Label, label, "autoSidebar/item_label");
    BRLS_BIND(brls::Label, subtitle, "autoSidebar/subtitle_label");
    BRLS_BIND(Box, icon_box, "autoSidebar/item_label_box");
    BRLS_BIND(SVGImage, icon, "autoSidebar/item_icon");

    brls::GenericEvent activeEvent;

    AutoSidebarItemGroup* group = nullptr;

    AutoTabBarStyle tabStyle = AutoTabBarStyle::NONE;

    NVGcolor tabItemBackgroundColor = nvgRGBA(0, 0, 0, 0);
    NVGcolor tabItemActiveBackgroundColor = nvgRGBA(0, 0, 0, 0);
    NVGcolor tabItemActiveTextColor = brls::Application::getTheme()["brls/text"];

    /// applies the "pill" colors (ACCENT horizontal mode) based on active state
    void applyPillStyle();

    bool active = false;
    bool horizontal = false;
    View* attachedView = nullptr;
    TabViewCreator attachedViewCreator = nullptr;
    std::string iconDefault, iconActivate;
};

class AutoSidebarItemGroup {
public:
    void add(AutoSidebarItem* item);
    void setActive(AutoSidebarItem* item);
    void clear();
    void removeView(AutoSidebarItem* view);
    int getActiveIndex();

private:
    std::vector<AutoSidebarItem*> items;
};

class AttachedView : public brls::Box {
public:
    AttachedView();

    void setTabBar(AutoSidebarItem* view);
    AutoSidebarItem* getTabBar();

    ~AttachedView() override;

    virtual void onCreate();

    View* getDefaultFocus() override { return brls::Box::getDefaultFocus(); }

    void registerTabAction(std::string hintText, enum brls::ControllerButton button, const brls::BrlsKeyCombination key,
        brls::ActionListener action, bool hidden = false, bool allowRepeating = false,
        enum brls::Sound sound = brls::SOUND_NONE);

private:
    AutoSidebarItem* tab = nullptr;
};

class AutoTabFrame : public brls::Box {
public:
    AutoTabFrame();
    void handleXMLElement(tinyxml2::XMLElement* element) override;
    void addTab(AutoSidebarItem* tab, TabViewCreator creator);
    /// insert the tab at the given position in the tab bar
    void addTab(AutoSidebarItem* tab, TabViewCreator creator, size_t position);
    void focusTab(int position);
    void clearTabs();
    void clearTab(const std::string& name, bool onlyFirst = true);
    bool isHaveTab(const std::string& name);
    AutoSidebarItem* getTab(const std::string& name);
    AutoSidebarItem* getTab(size_t index);

    static brls::View* create();
    ~AutoTabFrame() override;

    void setTabAttachedView(brls::View* newContent);

    void setDefaultTabIndex(size_t index);

    size_t getDefaultTabIndex();

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

    void setItemDefaultBackgroundColor(NVGcolor c);

    void setItemActiveBackgroundColor(NVGcolor c);

    void setItemActiveTextColor(NVGcolor c);

    void setFontSize(float size);

    float getFontSize();

    void setHorizontalMode(bool value);

    bool getHorizontalMode();

    void setDemandMode(bool value);

    void addItem(AutoSidebarItem* tab, TabViewCreator creator, brls::GenericEvent::Callback focusCallback);

    void addItem(
        AutoSidebarItem* tab, TabViewCreator creator, brls::GenericEvent::Callback focusCallback, size_t position);

    AutoSidebarItem* getItem(int position);

    void clearItems();

    Box* getSidebar();

    View* getActiveTab();

    void registerTabAction(View* view);

    static void focus2Sidebar(View* tabView);

    virtual void willAppear(bool resetState = false) override;

    virtual void willDisappear(bool resetState = false) override;

    bool isOnTop = false;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

    /// floating TOP mode: the bar (child 0, painted last) must also win the
    /// touch hit-test against the full-height content
    brls::View* hitTest(brls::Point point) override;

    /**
     * Setting the position of sidebar.
     * Only set once.
     */
    void setSideBarPosition(AutoTabBarPosition position);

    int getActiveIndex();

    void setTabChangedAction(const std::function<void(size_t)>& event);

    /// ---- detail view stack (movie/show/season/actor pages...) ----
    /// Stacks `detail` in the content area: the sidebar stays visible, the
    /// covered content goes GONE (never destroyed), the view gets focus
    /// and its B action pops. Multi-level stack (movie page -> actor page...).
    void pushDetailView(brls::View* detail);

    /// Pops and destroys the top view; restores the visibility of the
    /// covered content and the remembered focus (revalidated in the live
    /// tree: never focus a dead view). false if the stack is empty.
    bool popDetailView();

    bool hasDetailView();

    brls::View* getTopDetailView();

    /// clears the stack without focus restoration (tab change,
    /// destruction); the tab content becomes visible again
    void clearDetailViews();

private:
    /// focuses the top detail, retried every frame while its async content
    /// has nothing focusable (sidebar fallback while waiting —
    /// cf. implementation)
    void retryDetailFocus(brls::View* detail, int attemptsLeft);

    BRLS_BIND(Box, sidebar, "auto_tab_frame/auto_sidebar");

    struct DetailEntry {
        View* view = nullptr;
        /// focus at push time — pointer potentially dead at pop time,
        /// never dereferenced without revalidation (address comparison)
        View* previousFocus = nullptr;
        /// if the focus lived in a recycler: (recycler, cell index).
        /// Cells get recycled/rebound while the content is hidden
        /// (reloadData on relayout) — the cell pointer would stay
        /// "alive" but would show a different media.
        View* previousRecycler = nullptr;
        size_t previousIndex = 0;
    };
    std::vector<DetailEntry> detailStack;

    View* activeTab = nullptr;
    AutoTabBarPosition tabBarPosition = AutoTabBarPosition::LEFT;

    AutoSidebarItemGroup group;
    bool isHorizontal = false;
    bool isDemandMode = true;  // load pages on demand
    float itemFontSize = 22;
    /// tightened vertical sidebar: 28px centered icon + 4px accent flush
    /// against the right edge (76px stayed too wide, accent detached)
    float sidebarWidth = 64;

    std::function<void(size_t)> tabChangedAction = nullptr;

    void focus2NextTab();
    void focus2LastTab();

    NVGcolor skeletonBackground = brls::Application::getTheme()["color/grey_3"];
    NVGcolor tabItemBackgroundColor = nvgRGBA(0, 0, 0, 0);
    NVGcolor tabItemActiveBackgroundColor = nvgRGBA(0, 0, 0, 0);
    NVGcolor tabItemActiveTextColor = brls::Application::getTheme()["brls/text"];
};

namespace ui {

/// Presents `detail` on top of the main tab frame content: walks the tree
/// up from `from` (any live view on screen) to the OUTERMOST AutoTabFrame
/// (the MainTabFrame in practice — not the nested library tabs) and calls
/// pushDetailView: the sidebar stays visible and navigable, B pops.
/// Falls back to the old View::present (full-screen AppletFrame) when no
/// AutoTabFrame is found (e.g. view presented outside the sidebar:
/// presented search results, server list).
void presentDetail(brls::View* from, brls::View* detail);

/// Pops the top detail view if `from` lives inside it (close cross, touch
/// equivalent of B). false if `from` is not in a stacked detail view —
/// the caller then falls back to View::dismiss().
bool popDetail(brls::View* from);

}  // namespace ui
