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

    /// applique les couleurs « pill » (mode horizontal ACCENT) selon l'état actif
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

    /// mode TOP flottant : la barre (enfant 0, peinte en dernier) doit aussi
    /// gagner le hit-test tactile face au contenu pleine hauteur
    brls::View* hitTest(brls::Point point) override;

    /**
     * Setting the position of sidebar.
     * Only set once.
     */
    void setSideBarPosition(AutoTabBarPosition position);

    int getActiveIndex();

    void setTabChangedAction(const std::function<void(size_t)>& event);

    /// ---- pile de vues de détail (fiches film/série/saison/acteur…) ----
    /// Empile `detail` dans la zone de contenu : la sidebar reste visible, le
    /// contenu couvert passe en GONE (jamais détruit), la vue reçoit le focus
    /// et son action B dépile. Pile multi-niveaux (fiche film → fiche acteur…).
    void pushDetailView(brls::View* detail);

    /// Dépile et détruit la vue du dessus ; restaure la visibilité du contenu
    /// couvert et le focus mémorisé (revalidé dans l'arbre vivant : jamais de
    /// focus sur une vue morte). false si la pile est vide.
    bool popDetailView();

    bool hasDetailView();

    brls::View* getTopDetailView();

    /// vide la pile sans restauration de focus (changement d'onglet,
    /// destruction) ; le contenu de l'onglet redevient visible
    void clearDetailViews();

private:
    /// focus du détail au sommet, retenté chaque frame tant que son contenu
    /// asynchrone n'a rien de focusable (repli sidebar pendant l'attente —
    /// cf. implémentation)
    void retryDetailFocus(brls::View* detail, int attemptsLeft);

    BRLS_BIND(Box, sidebar, "auto_tab_frame/auto_sidebar");

    struct DetailEntry {
        View* view = nullptr;
        /// focus au moment du push — pointeur potentiellement mort au pop,
        /// jamais déréférencé sans revalidation (comparaison d'adresses)
        View* previousFocus = nullptr;
        /// si le focus vivait dans un recycler : (recycler, index de la
        /// cellule). Les cellules sont recyclées/rebindées pendant que le
        /// contenu est masqué (reloadData au relayout) — le pointeur de
        /// cellule resterait « vivant » mais montrerait un autre média.
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
    /// sidebar verticale resserrée : icône 28px centrée + accent 4px collé au
    /// bord droit (recette UI n°5 : 76px restait trop large, accent décollé)
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

/// Présente `detail` par-dessus le contenu du tab frame principal : remonte
/// l'arbre depuis `from` (n'importe quelle vue vivante de l'écran) jusqu'au
/// AutoTabFrame le plus EXTERNE (le MainTabFrame en pratique — pas les
/// onglets imbriqués des bibliothèques) et appelle pushDetailView : la
/// sidebar reste visible et navigable, B dépile. Repli sur l'ancien
/// View::present (AppletFrame plein écran) quand aucun AutoTabFrame n'est
/// trouvé (ex. vue présentée hors sidebar : résultats de recherche présentés,
/// liste de serveurs).
void presentDetail(brls::View* from, brls::View* detail);

/// Dépile la vue de détail du dessus si `from` vit dedans (croix de
/// fermeture, équivalent tactile de B). false si `from` n'est pas dans une
/// vue de détail empilée — l'appelant retombe alors sur View::dismiss().
bool popDetail(brls::View* from);

}  // namespace ui
