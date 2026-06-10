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

#include <algorithm>
#include <tinyxml2.h>
#include "view/auto_tab_frame.hpp"
#include "view/svg_image.hpp"
#include "view/recycling_grid.hpp"
#include "view/h_recycling.hpp"
#include "utils/keybind.hpp"

/**
 * auto tab frame
 */
using namespace brls::literals;

/// true si `view` appartient à la chaîne de parents menant à `root`
/// (les deux pointeurs sont vivants : parcours ascendant sûr)
static bool isDescendantOf(brls::View* view, brls::View* root) {
    for (brls::View* v = view; v; v = v->getParent()) {
        if (v == root) return true;
    }
    return false;
}

/// premier brls::ScrollingFrame du sous-arbre (DFS) : la grille/liste dont
/// la barre d'onglets TOP suit le défilement (RecyclingGrid en hérite).
/// nullptr si l'onglet ne défile pas → barre fixe à 0.
static brls::ScrollingFrame* findScrollingFrame(brls::View* root) {
    if (auto* frame = dynamic_cast<brls::ScrollingFrame*>(root)) return frame;
    if (auto* box = dynamic_cast<brls::Box*>(root)) {
        for (auto* child : box->getChildren()) {
            if (auto* frame = findScrollingFrame(child)) return frame;
        }
    }
    return nullptr;
}

/// true si `needle` est dans le sous-arbre VIVANT de `root`. `needle` n'est
/// JAMAIS déréférencé (comparaison d'adresses) : c'est la revalidation d'un
/// pointeur de focus potentiellement mort avant restauration.
static bool treeContains(brls::View* root, brls::View* needle) {
    if (root == needle) return true;
    auto* box = dynamic_cast<brls::Box*>(root);
    if (!box) return false;
    for (auto* child : box->getChildren()) {
        if (treeContains(child, needle)) return true;
    }
    return false;
}

const std::string autoTabFrameContentXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        axis="row">

        <brls:Box
            wireframe="false"
            id="auto_tab_frame/auto_sidebar"
            width="64"
            height="auto" />

        <!-- Content will be injected here with grow="1.0" -->

    </brls:Box>
)xml";

brls::View* AutoTabFrame::create() { return new AutoTabFrame(); }

AutoTabFrame::AutoTabFrame() {
    this->inflateFromXMLString(autoTabFrameContentXML);

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE("sidebarPosition", AutoTabBarPosition, this->setSideBarPosition,
        {
            {"top", AutoTabBarPosition::TOP},
            {"right", AutoTabBarPosition::RIGHT},
            {"left", AutoTabBarPosition::LEFT},
        });

    // this only works with "sidebarPosition == left"
    // and It must be set before you set the sidebarPosition
    this->registerFloatXMLAttribute("sidebarWidth", [this](float value) { this->sidebarWidth = value; });

    this->registerFloatXMLAttribute("tabFontSize", [this](float value) { this->setFontSize(value); });

    this->registerFloatXMLAttribute("tabHeight", [this](float value) { this->sidebar->setHeight(value); });

    this->registerColorXMLAttribute(
        "tabBackgroundColor", [this](NVGcolor value) { this->sidebar->setBackgroundColor(value); });

    // alignment of the items along the tab bar (e.g. centered top tabs)
    BRLS_REGISTER_ENUM_XML_ATTRIBUTE("tabJustifyContent", brls::JustifyContent, this->sidebar->setJustifyContent,
        {
            {"flexStart", brls::JustifyContent::FLEX_START},
            {"center", brls::JustifyContent::CENTER},
            {"flexEnd", brls::JustifyContent::FLEX_END},
            {"spaceBetween", brls::JustifyContent::SPACE_BETWEEN},
            {"spaceAround", brls::JustifyContent::SPACE_AROUND},
            {"spaceEvenly", brls::JustifyContent::SPACE_EVENLY},
        });

    this->registerColorXMLAttribute(
        "tabItemDefaultBackgroundColor", [this](NVGcolor value) { this->setItemDefaultBackgroundColor(value); });

    this->registerColorXMLAttribute(
        "tabItemActiveBackgroundColor", [this](NVGcolor value) { this->setItemActiveBackgroundColor(value); });

    this->registerColorXMLAttribute(
        "tabItemActiveTextColor", [this](NVGcolor value) { this->setItemActiveTextColor(value); });

    // defaultTab: default is 0
    this->registerFloatXMLAttribute("defaultTab", [this](float value) { this->setDefaultTabIndex(value); });

    // default is true, only load pages on demand
    this->registerBoolXMLAttribute("demandMode", [this](bool value) { this->setDemandMode(value); });

    this->sidebar->setAxis(brls::Axis::COLUMN);
    // paddings latéraux 0 : items pleine largeur (le fond de focus couvre
    // la sidebar de bord à bord) ; l'accent (marginRight 0) affleure le
    // bord droit — zéro pixel d'espacement
    this->sidebar->setPadding(32, 0, 47, 0);
}

void AutoTabFrame::setTabChangedAction(const std::function<void(size_t)>& event) { this->tabChangedAction = event; }

void AutoTabFrame::setDemandMode(bool value) { this->isDemandMode = value; }

void AutoTabFrame::setSideBarPosition(AutoTabBarPosition position) {
    this->tabBarPosition = position;
    switch (position) {
    case AutoTabBarPosition::TOP:
        this->setAxis(brls::Axis::COLUMN);
        this->setDirection(brls::Direction::LEFT_TO_RIGHT);
        this->setHorizontalMode(true);
        // barre FLOTTANTE : sortie du flux (absolute), pleine largeur, fond
        // transparent (les XML top n'en posent pas) — le contenu occupe tout
        // le frame et défile dessous (paddingTop interne ~70 côté grilles) ;
        // dessinée en dernier dans draw() pour rester au-dessus
        this->sidebar->setPositionType(brls::PositionType::ABSOLUTE);
        this->sidebar->setPositionTop(0);
        this->sidebar->setPositionLeft(0);
        this->sidebar->setWidthPercentage(100);
        break;
    case AutoTabBarPosition::RIGHT:
        this->setAxis(brls::Axis::ROW);
        this->setDirection(brls::Direction::RIGHT_TO_LEFT);
        this->setHorizontalMode(false);
        this->sidebar->setWidth(sidebarWidth);
        break;
    case AutoTabBarPosition::LEFT:
        this->setAxis(brls::Axis::ROW);
        this->setDirection(brls::Direction::LEFT_TO_RIGHT);
        this->setHorizontalMode(false);
        this->sidebar->setWidth(sidebarWidth);
        break;
    default:;
    }
    this->invalidate();
}

int AutoTabFrame::getActiveIndex() { return this->group.getActiveIndex(); }

void AutoTabFrame::addTab(AutoSidebarItem* tab, TabViewCreator creator) {
    this->addTab(tab, std::move(creator), this->sidebar->getChildren().size());
}

void AutoTabFrame::addTab(AutoSidebarItem* tab, TabViewCreator creator, size_t position) {
    tab->setDefaultBackgroundColor(this->tabItemBackgroundColor);
    tab->setActiveBackgroundColor(this->tabItemActiveBackgroundColor);
    tab->setActiveTextColor(this->tabItemActiveTextColor);

    this->addItem(
        tab, std::move(creator),
        [this](brls::View* view) {
            auto* sidebarItem = (AutoSidebarItem*)view;

            // Only trigger when the sidebar item gains focus
            if (!view->isFocused()) return;

            // Add the new tab
            View* newContent = sidebarItem->getAttachedView();
            if (!newContent) {
                newContent = sidebarItem->createAttachedView();
            }

            if (newContent == this->getActiveTab()) return;

            this->setTabAttachedView(newContent);

            if (this->tabChangedAction) this->tabChangedAction(sidebarItem->getCurrentIndex());
        },
        position);
    auto isDefaultTab = position == this->getDefaultTabIndex();

    if (isDefaultTab || !isDemandMode) {
        auto* item = (AutoSidebarItem*)this->sidebar->getChildren()[position];
        View* newContent = item->getAttachedView();
        if (!newContent) {
            newContent = item->createAttachedView();
        }

        if (isDefaultTab) {
            this->group.setActive(item);
            this->setTabAttachedView(newContent);
        }
    }
}

void AutoTabFrame::focusTab(int position) { brls::Application::giveFocus(this->getItem(position)); }

void AutoTabFrame::registerTabAction(brls::View* view) {
    view->registerAction(
        "last", brls::BUTTON_LB,
        [this](brls::View* view) {
            this->focus2LastTab();
            return true;
        },
        true);

    view->registerAction(KeyBind::getLast(), [this](brls::View* view) {
        this->focus2LastTab();
        return true;
    });

    this->registerAction(
        "next", brls::BUTTON_RB,
        [this](brls::View* view) {
            this->focus2NextTab();
            return true;
        },
        true);

    this->registerAction(KeyBind::getNext(), [this](brls::View* view) {
        this->focus2NextTab();
        return true;
    });
}

void AutoTabFrame::focus2NextTab() {
    size_t sideBarNum = this->sidebar->getChildren().size();
    if (sideBarNum == 0) return;

    int currentIndex = this->group.getActiveIndex();
    if (currentIndex < 0) {
        // not found
        this->focusTab(0);
    } else if (sideBarNum == 1) {
        // shake highlight (currentFocus peut être nul pendant une destruction)
        brls::View* focus = brls::Application::getCurrentFocus();
        if (focus) focus->shakeHighlight(this->isHorizontal ? brls::FocusDirection::RIGHT : brls::FocusDirection::DOWN);
    } else if (currentIndex + 1 >= (int)sideBarNum) {
        // loop
        this->focusTab(0);
    } else {
        this->focusTab(currentIndex + 1);
    }
}

void AutoTabFrame::focus2LastTab() {
    size_t sideBarNum = this->sidebar->getChildren().size();
    if (sideBarNum == 0) return;

    int currentIndex = this->group.getActiveIndex();
    if (currentIndex < 0) {
        // not found
        this->focusTab(0);
    } else if (sideBarNum == 1) {
        // shake highlight (currentFocus peut être nul pendant une destruction)
        brls::View* focus = brls::Application::getCurrentFocus();
        if (focus) focus->shakeHighlight(this->isHorizontal ? brls::FocusDirection::LEFT : brls::FocusDirection::UP);
    } else if (currentIndex == 0) {
        // loop
        this->focusTab(sideBarNum - 1);
    } else {
        this->focusTab(currentIndex - 1);
    }
}

void AutoTabFrame::clearTabs() { this->clearItems(); }

void AutoTabFrame::clearTab(const std::string& name, bool onlyFirst) {
    for (auto& i : this->sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item && (item->getLabel() == name)) {
            // maybe something wrong will happen ?
            if (item->isFocused()) {
                this->setLastFocusedView(nullptr);
                brls::Application::giveFocus(this);
            }
            this->sidebar->removeView(item, true);
            this->group.removeView(item);

            if (onlyFirst) break;
        }
    }
}

bool AutoTabFrame::isHaveTab(const std::string& name) {
    for (auto& i : this->sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item && (item->getLabel() == name)) return true;
    }
    return false;
}

AutoSidebarItem* AutoTabFrame::getTab(const std::string& name) {
    for (auto& i : this->sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item && (item->getLabel() == name)) return item;
    }
    return nullptr;
}

AutoSidebarItem* AutoTabFrame::getTab(size_t value) {
    size_t index = 0;
    for (auto& i : this->sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item && index == value) {
            return item;
        }
        index++;
    }
    return nullptr;
}

void AutoTabFrame::handleXMLElement(tinyxml2::XMLElement* element) {
    std::string name = element->Name();

    if (name == "Tab") {
        const tinyxml2::XMLAttribute* labelAttribute = element->FindAttribute("label");
        const tinyxml2::XMLAttribute* styleAttribute = element->FindAttribute("style");

        if (!labelAttribute) brls::fatal("\"label\" attribute missing from \"" + name + "\" tab");

        std::string tabStyle = "accent";

        if (styleAttribute) tabStyle = View::getStringXMLAttributeValue(styleAttribute->Value());

        AutoSidebarItem* item = new AutoSidebarItem();
        item->applyXMLAttribute("style", tabStyle);
        item->applyXMLAttributes(element);

        tinyxml2::XMLElement* viewElement = element->FirstChildElement();

        if (viewElement) {
            this->addTab(item, [viewElement] { return View::createFromXMLElement(viewElement); });

            if (viewElement->NextSiblingElement()) brls::fatal("\"Tab\" can only contain one child element");
        } else {
            this->addTab(item, [] { return nullptr; });
        }
    } else {
        brls::fatal("Unknown child element \"" + name + "\" for \"brls:Tab\"");
    }
}

AutoTabFrame::~AutoTabFrame() {
    brls::Logger::debug("delete AutoTabFrame");
    // détacher les fiches empilées (libérées via la deletionPool)
    this->clearDetailViews();
    if (this->activeTab) {
        // 直接移除activeTab，销毁的工作交给其对应的 AutoSidebarItem 来处理
        auto& children = this->getChildren();
        auto it = std::find(children.begin(), children.end(), this->activeTab);
        if (it != children.end()) children.erase(it);
        this->activeTab = nullptr;
    }
}

void AutoTabFrame::setTabAttachedView(brls::View* newContent) {
    // un changement d'onglet abandonne la pile de fiches : le contenu de
    // l'onglet redevient la seule vue de la zone de contenu
    this->clearDetailViews();
    // Remove the existing tab if it exists
    if (this->activeTab) {
        // will call willDisappear but not delete
        this->removeView(this->activeTab, false);
        this->activeTab = nullptr;
    }
    if (!newContent) {
        return;
    }
    newContent->setGrow(1.0f);
    this->addView(newContent);  // addView calls willAppear
    this->activeTab = newContent;
}

void AutoTabFrame::pushDetailView(brls::View* detail) {
    if (!detail) return;

    brls::View* covered = this->detailStack.empty() ? this->activeTab : this->detailStack.back().view;

    DetailEntry entry;
    entry.view = detail;
    entry.previousFocus = brls::Application::getCurrentFocus();
    // focus dans un recycler : mémoriser (recycler, index) pour une
    // restauration par INDEX au pop (les pointeurs de cellules se font
    // rebinder sur d'autres médias pendant que le contenu est masqué)
    for (brls::View* v = entry.previousFocus; v && v != this; v = v->getParent()) {
        if (auto* cell = dynamic_cast<RecyclingGridItem*>(v)) {
            entry.previousIndex = cell->getIndex();
        } else if (dynamic_cast<RecyclingView*>(v)) {
            entry.previousRecycler = v;
            break;
        }
    }

    detail->setGrow(1.0f);
    detail->registerAction(
        "hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) { return this->popDetailView(); }, false, false,
        brls::SOUND_BACK);

    this->addView(detail);  // appelle willAppear
    // masqué SANS être détruit : état (scroll, données, focus interne) intact
    if (covered) covered->setVisibility(brls::Visibility::GONE);
    this->detailStack.push_back(entry);

    // le focus peut ne pas se résoudre avant plusieurs frames : un détail
    // fraîchement poussé n'a parfois RIEN de focusable tant que sa requête
    // n'a pas répondu (skeletons — ex. « aller à la saison »).
    this->retryDetailFocus(detail, 600);
}

/// Donne le focus au détail au sommet de la pile, en réessayant à chaque
/// frame tant que rien n'y est focusable (contenu asynchrone). Pendant
/// l'attente, le focus est posé sur la SIDEBAR : le laisser sur la vue
/// couverte (GONE) dessinait un halo fantôme à frame dégénérée et faisait
/// naviguer dans un arbre masqué, jusqu'au segfault sur des cellules
/// recyclées/détruites (recette n°6).
void AutoTabFrame::retryDetailFocus(brls::View* detail, int attemptsLeft) {
    // détail dépilé (B rapide) ou recouvert entre-temps : ne rien voler
    if (this->detailStack.empty() || this->detailStack.back().view != detail) return;
    brls::View* focus = brls::Application::getCurrentFocus();
    if (focus && isDescendantOf(focus, detail)) return;  // résolu
    brls::Application::giveFocus(detail);
    focus = brls::Application::getCurrentFocus();
    if (focus && isDescendantOf(focus, detail)) return;  // résolu
    if (!focus || !isDescendantOf(focus, this->sidebar)) brls::Application::giveFocus(this->sidebar);
    if (attemptsLeft <= 0) return;  // état sain (sidebar), on arrête là
    ASYNC_RETAIN
    brls::sync([ASYNC_TOKEN, detail, attemptsLeft]() {
        ASYNC_RELEASE
        this->retryDetailFocus(detail, attemptsLeft - 1);
    });
}

bool AutoTabFrame::popDetailView() {
    if (this->detailStack.empty()) return false;

    DetailEntry entry = this->detailStack.back();
    this->detailStack.pop_back();

    brls::View* uncovered = this->detailStack.empty() ? this->activeTab : this->detailStack.back().view;
    if (uncovered) uncovered->setVisibility(brls::Visibility::VISIBLE);

    // détache la fiche ; freeView (via removeView) diffère la destruction en
    // fin de frame (deletionPool) — sûr même depuis l'action B de cette fiche
    this->removeView(entry.view, true);

    // focus dans un recycler au moment du push : restauration par INDEX à la
    // frame suivante (laisse le relayout du contenu démasqué — et son
    // éventuel reloadData — se faire d'abord), via selectRowAt qui scrolle et
    // re-matérialise la bonne cellule
    if (uncovered && entry.previousRecycler && treeContains(uncovered, entry.previousRecycler)) {
        brls::View* recycler = entry.previousRecycler;
        size_t index = entry.previousIndex;
        brls::Application::giveFocus(uncovered);
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN, recycler, index]() {
            ASYNC_RELEASE
            // le recycler vit dans l'onglet/fiche découvert : il ne peut pas
            // avoir disparu en une frame sans interaction utilisateur
            auto* source = dynamic_cast<RecyclingView*>(recycler)->getDataSource();
            if (source && index < source->getItemCount()) {
                if (auto* grid = dynamic_cast<RecyclingGrid*>(recycler))
                    grid->selectRowAt(index, false);
                else if (auto* row = dynamic_cast<HRecyclerFrame*>(recycler))
                    row->selectRowAt(index, false);
            }
            brls::Application::giveFocus(recycler);
        });
        return true;
    }

    // sinon : restaure le pointeur mémorisé seulement s'il vit toujours dans
    // le contenu découvert ; sinon le conteneur résout son focus par défaut
    brls::View* target = nullptr;
    if (uncovered && entry.previousFocus && treeContains(uncovered, entry.previousFocus))
        target = entry.previousFocus;
    if (!target) target = uncovered;
    if (target) brls::Application::giveFocus(target);
    return true;
}

bool AutoTabFrame::hasDetailView() { return !this->detailStack.empty(); }

brls::View* AutoTabFrame::getTopDetailView() {
    return this->detailStack.empty() ? nullptr : this->detailStack.back().view;
}

void AutoTabFrame::clearDetailViews() {
    if (this->detailStack.empty()) return;
    for (auto it = this->detailStack.rbegin(); it != this->detailStack.rend(); ++it) {
        this->removeView(it->view, true);  // destruction différée (deletionPool)
    }
    this->detailStack.clear();
    if (this->activeTab) this->activeTab->setVisibility(brls::Visibility::VISIBLE);
}

void ui::presentDetail(brls::View* from, brls::View* detail) {
    // AutoTabFrame le plus externe : ignore les onglets imbriqués (onglets
    // horizontaux des bibliothèques) pour couvrir toute la zone de contenu
    AutoTabFrame* frame = nullptr;
    for (brls::View* v = from; v; v = v->getParent()) {
        if (auto* f = dynamic_cast<AutoTabFrame*>(v)) frame = f;
    }
    if (frame) {
        frame->pushDetailView(detail);
    } else if (from) {
        // hors sidebar (recherche présentée, liste de serveurs…)
        from->present(detail);
    }
}

bool ui::popDetail(brls::View* from) {
    AutoTabFrame* frame = nullptr;
    for (brls::View* v = from; v; v = v->getParent()) {
        if (auto* f = dynamic_cast<AutoTabFrame*>(v)) frame = f;
    }
    if (!frame || !frame->hasDetailView()) return false;
    if (!isDescendantOf(from, frame->getTopDetailView())) return false;
    return frame->popDetailView();
}

void AutoTabFrame::setDefaultTabIndex(size_t index) { this->sidebar->setDefaultFocusedIndex(index); }

size_t AutoTabFrame::getDefaultTabIndex() { return this->sidebar->getDefaultFocusedIndex(); }

brls::View* AutoTabFrame::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    // Do not navigate down, except through sidebar area
    if (direction == brls::FocusDirection::DOWN && currentView != this->sidebar) {
        return nullptr;
    }

    void* parentUserData = currentView->getParentUserData();

    // Return nullptr immediately if focus direction mismatches the box axis (clang-format refuses to split it in multiple lines...)
    if ((this->getAxis() == brls::Axis::ROW && direction != brls::FocusDirection::LEFT &&
            direction != brls::FocusDirection::RIGHT) ||
        (this->getAxis() == brls::Axis::COLUMN && direction != brls::FocusDirection::UP &&
            direction != brls::FocusDirection::DOWN)) {
        View* next = getParentNavigationDecision(this, nullptr, direction);
        if (!next && hasParent()) next = getParent()->getNextFocus(direction, this);
        return next;
    }

    // Traverse the children
    size_t offset = 1;  // which way we are going in the children list

    if ((this->getAxis() == brls::Axis::ROW && direction == brls::FocusDirection::LEFT &&
            tabBarPosition == AutoTabBarPosition::LEFT) ||
        (this->getAxis() == brls::Axis::ROW && direction == brls::FocusDirection::RIGHT &&
            tabBarPosition == AutoTabBarPosition::RIGHT) ||
        (this->getAxis() == brls::Axis::COLUMN && direction == brls::FocusDirection::UP)) {
        offset = -1;
    }

    size_t currentFocusIndex = *((size_t*)parentUserData) + offset;
    View* currentFocus = nullptr;

    while (!currentFocus && currentFocusIndex >= 0 && currentFocusIndex < this->getChildren().size()) {
        // les vues GONE (contenu d'onglet couvert par une fiche, fiches
        // enfouies sous la pile) ne doivent jamais recevoir le focus :
        // isFocusable() ne vérifie que la visibilité de la feuille, pas
        // celle de ses ancêtres
        View* child = this->getChildren()[currentFocusIndex];
        if (child->getVisibility() == brls::Visibility::VISIBLE) currentFocus = child->getDefaultFocus();
        currentFocusIndex += offset;
    }

    currentFocus = getParentNavigationDecision(this, currentFocus, direction);
    if (!currentFocus && hasParent()) currentFocus = getParent()->getNextFocus(direction, this);
    return currentFocus;
}

void AutoTabFrame::setFontSize(float size) {
    this->itemFontSize = size;
    for (auto item : this->sidebar->getChildren()) {
        ((AutoSidebarItem*)item)->setFontSize(size);
    }
}

float AutoTabFrame::getFontSize() { return this->itemFontSize; }

void AutoTabFrame::setHorizontalMode(bool value) {
    this->isHorizontal = value;
    for (auto item : this->sidebar->getChildren()) {
        ((AutoSidebarItem*)item)->setHorizontalMode(value);
    }
    if (value) {
        this->sidebar->setPadding(8, 20, 8, 20);
        this->sidebar->setAxis(brls::Axis::ROW);
        // pills de 34px centrées dans la barre (tabHeight 60)
        this->sidebar->setAlignItems(brls::AlignItems::CENTER);
    } else {
        this->sidebar->setPadding(32, 0, 47, 0);
        this->sidebar->setAxis(brls::Axis::COLUMN);
        this->sidebar->setAlignItems(brls::AlignItems::STRETCH);
    }

    this->invalidate();
}

bool AutoTabFrame::getHorizontalMode() { return this->isHorizontal; }

void AutoTabFrame::addItem(AutoSidebarItem* tab, TabViewCreator creator, brls::GenericEvent::Callback focusCallback) {
    this->addItem(tab, std::move(creator), std::move(focusCallback), this->sidebar->getChildren().size());
}

void AutoTabFrame::addItem(
    AutoSidebarItem* tab, TabViewCreator creator, brls::GenericEvent::Callback focusCallback, size_t position) {
    tab->setAttachedViewCreator(creator);
    tab->setHorizontalMode(this->isHorizontal);
    tab->setGroup(&this->group);
    tab->getActiveEvent()->subscribe(focusCallback);

    this->sidebar->addView(tab, position);
}

AutoSidebarItem* AutoTabFrame::getItem(int position) {
    return dynamic_cast<AutoSidebarItem*>(this->sidebar->getChildren()[position]);
}

void AutoTabFrame::clearItems() {
    this->setTabAttachedView(nullptr);
    this->sidebar->clearViews();
    this->group.clear();
    this->setLastFocusedView(nullptr);
}

brls::Box* AutoTabFrame::getSidebar() { return this->sidebar; }

brls::View* AutoTabFrame::getActiveTab() { return this->activeTab; }

void AutoTabFrame::focus2Sidebar(View* tabView) {
    AutoTabFrame* frame = dynamic_cast<AutoTabFrame*>(tabView->getParent());
    if (frame && frame->isOnTop) {
        brls::Application::giveFocus(frame->getSidebar());
    }
}

void AutoTabFrame::willAppear(bool resetState) {
    this->isOnTop = true;
    Box::willAppear(resetState);
}

void AutoTabFrame::willDisappear(bool resetState) {
    this->isOnTop = false;
    Box::willDisappear(resetState);
}

void AutoTabFrame::setItemDefaultBackgroundColor(NVGcolor c) {
    tabItemBackgroundColor = c;
    for (auto i : sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item) {
            item->setDefaultBackgroundColor(c);
        }
    }
}

void AutoTabFrame::setItemActiveBackgroundColor(NVGcolor c) {
    tabItemActiveBackgroundColor = c;
    for (auto i : sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item) {
            item->setActiveBackgroundColor(c);
        }
    }
}

void AutoTabFrame::setItemActiveTextColor(NVGcolor c) {
    tabItemActiveTextColor = c;
    for (auto i : sidebar->getChildren()) {
        AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(i);
        if (item) {
            item->setActiveTextColor(c);
        }
    }
}

void AutoTabFrame::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    //todo: 最后绘制刷新按钮

    if (this->sidebar && this->sidebar->getChildren().size() == 0) {
        // Draw skeleton screen
        // Only fit to home_bangumi and home_cinema page

        brls::Time curTime = brls::getCPUTimeUsec() / 1000;
        float p = (curTime % 1000) * 1.0 / 1000;
        p = fabs(0.5 - p) + 0.25;

        float padding = 20;
        auto drawWidth = width - 3 * padding;
        auto drawHeight = height - padding;
        auto drawX = x + padding + getMarginLeft();
        auto drawY = y + padding;
        auto sidebarHeight = this->sidebar->getHeight() - 10;

        if (this->isHorizontal) {
            drawHeight -= sidebarHeight;
            drawY += sidebarHeight;
        }

        NVGcolor end = skeletonBackground;
        end.a = p;
        NVGpaint paint =
            nvgLinearGradient(vg, drawX, drawY, drawX + drawWidth, drawY + drawHeight, a(skeletonBackground), a(end));
        nvgBeginPath(vg);
        nvgFillPaint(vg, paint);
        nvgRoundedRect(vg, drawX, drawY, drawWidth, drawHeight, 6);
        nvgFill(vg);

        if (!this->isHorizontal) return;

        // draw sidebar items
        const unsigned int num = 6;
        const unsigned int itemWidth = 80;
        drawY = y + 10;
        drawX = x + padding;
        padding = 10;

        for (size_t i = 0; i < num; i++) {
            paint = nvgLinearGradient(
                vg, drawX, drawY, drawX + itemWidth, drawY + sidebarHeight, a(skeletonBackground), a(end));
            nvgBeginPath(vg);
            nvgFillPaint(vg, paint);
            nvgRoundedRect(vg, drawX, drawY, itemWidth, sidebarHeight, 6);
            nvgFill(vg);
            drawX += padding + itemWidth;
        }
    }

    if (this->tabBarPosition == AutoTabBarPosition::TOP) {
        // barre solidaire du défilement (recette UI n°5) : translation de
        // -offset du premier ScrollingFrame de l'onglet actif — on scrolle,
        // la barre sort par le haut ; retour à 0, elle revient (même
        // mécanisme que contentView->setTranslationY(-offset) du
        // ScrollingFrame lui-même). Résolution à CHAQUE frame, sans cache de
        // pointeur : les contenus d'onglet se (re)construisent en asynchrone
        // (données reçues, reloads) et un pointeur retenu pendouillerait ; le
        // DFS s'arrête à la première grille (1-2 niveaux), coût négligeable.
        // La translation décale getFrame() de la barre et de ses enfants →
        // le hitTest ci-dessous suit tout seul : barre sortie de l'écran, les
        // taps re-atteignent la grille. Les paddingTop 70 internes aux
        // contenus restent (position de repos sous la barre). Changement
        // d'onglet : la barre saute à l'offset de la nouvelle grille (accepté).
        float scrollOffset = 0.0f;
        if (this->activeTab) {
            if (auto* scroller = findScrollingFrame(this->activeTab))
                scrollOffset = std::max(0.0f, scroller->getContentOffsetY());
        }
        this->sidebar->setTranslationY(-scrollOffset);

        // barre flottante : le contenu défile DESSOUS la barre, elle doit
        // donc être peinte en DERNIER — l'ordre des enfants (sidebar en tête,
        // indispensable aux index de navigation de getNextFocus) donnerait
        // l'inverse via Box::draw. frame() gère visibilité et alpha ; le test
        // de culling de Box::draw ne s'applique qu'aux feuilles non-Box,
        // jamais à ces enfants-là (sidebar et contenus sont des Box)
        for (auto* child : this->getChildren()) {
            if (child != this->sidebar) child->frame(ctx);
        }
        this->sidebar->frame(ctx);
        return;
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* AutoTabFrame::hitTest(brls::Point point) {
    if (this->tabBarPosition != AutoTabBarPosition::TOP) return Box::hitTest(point);

    // miroir tactile de l'ordre de dessin flottant : Box::hitTest parcourt
    // les enfants en ordre INVERSE (le dernier dessiné gagne), or la barre
    // est l'enfant 0 — le contenu pleine hauteur capterait tous les taps de
    // la zone de barre. On sonde donc la barre d'abord.
    if (this->getAlpha() == 0.0f || this->getVisibility() != brls::Visibility::VISIBLE) return nullptr;
    if (!this->getFrame().pointInside(point)) return nullptr;

    if (brls::View* result = this->sidebar->hitTest(point)) return result;
    auto& children = this->getChildren();
    for (auto it = children.rbegin(); it != children.rend(); it++) {
        if (*it == this->sidebar) continue;
        if (brls::View* result = (*it)->hitTest(point)) return result;
    }
    return this;
}

/**
 * auto sidebar item
 */

const std::string autoSidebarItemXML = R"xml(
    <brls:Box
        wireframe="false"
        width="auto"
        direction="leftToRight"
        height="auto"
        focusable="true" >

        <brls:Box
            wireframe="false"
            grow="1.0"
            width="auto"
            height="auto"
            justifyContent="center"
            alignItems="center"
            axis="column"
            marginTop="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginBottom="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginRight="@style/brls/sidebar/item_accent_margin_sides"
            id="autoSidebar/item_label_box">

            <!-- 28px : centrée dans la sidebar 64 (padding gauche 10 + 8,
                 icon_box 30 de large) sans clipping ni débord sur l'accent -->
            <SVGImage
                wireframe="false"
                visibility="gone"
                id="autoSidebar/item_icon"
                width="28"
                height="28"/>

            <brls:Label
                wireframe="false"
                id="autoSidebar/item_label"
                width="auto"
                height="auto"
                fontSize="22"
                marginBottom="5"
                horizontalAlign="center"/>

            <brls:Label
                wireframe="false"
                id="autoSidebar/subtitle_label"
                singleLine="true"
                width="auto"
                minWidth="80"
                height="auto"
                fontSize="12"
                textColor="#80808080"
                positionType="absolute"
                positionTop="-12"
                horizontalAlign="center"/>
        </brls:Box>

        <brls:Rectangle
            id="autoSidebar/item_accent"
            width="@style/brls/sidebar/item_accent_rect_width"
            height="auto"
            visibility="invisible"
            marginTop="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginBottom="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginLeft="@style/brls/sidebar/item_accent_margin_sides"
            marginRight="@style/brls/sidebar/item_accent_margin_sides" />

    </brls:Box>
)xml";

const std::string autoSidebarItemPlainXML = R"xml(
    <brls:Box
        highlightCornerRadius="8"
        cornerRadius="4"
        hideHighlightBackground="true"
        wireframe="false"
        width="auto"
        direction="rightToLeft"
        height="auto"
        marginLeft="8"
        marginRight="8"
        focusable="true" >

        <brls:Box
            wireframe="false"
            grow="1.0"
            width="auto"
            height="auto"
            justifyContent="center"
            alignItems="center"
            axis="column"
            id="autoSidebar/item_label_box">

            <SVGImage
                wireframe="false"
                visibility="gone"
                id="autoSidebar/item_icon"
                width="26"
                height="26"/>

            <brls:Label
                wireframe="false"
                id="autoSidebar/item_label"
                width="auto"
                height="auto"
                fontSize="22"
                horizontalAlign="center"/>

            <brls:Label
                wireframe="false"
                id="autoSidebar/subtitle_label"
                width="auto"
                height="auto"
                fontSize="12"
                positionType="absolute"
                positionTop="-12"
                horizontalAlign="center"/>

        </brls:Box>

        <brls:Rectangle
            id="autoSidebar/item_accent"
            width="@style/brls/sidebar/item_accent_rect_width"
            height="auto"
            visibility="gone"
            color="#FF6699"
            marginTop="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginBottom="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginLeft="@style/brls/sidebar/item_accent_margin_sides"
            marginRight="@style/brls/sidebar/item_accent_margin_sides" />

    </brls:Box>
)xml";

AutoSidebarItem::AutoSidebarItem() : Box(brls::Axis::ROW) {
    this->registerStringXMLAttribute("label", [this](std::string value) {
        this->label->setText(value);
        return true;
    });

    this->registerFloatXMLAttribute("fontSize", [this](float value) {
        this->label->setFontSize(value);
        return true;
    });

    this->registerFilePathXMLAttribute("icon", [this](std::string value) {
        this->iconDefault = value;
        this->icon->setVisibility(brls::Visibility::VISIBLE);
        this->icon->setImageFromSVGFile(value);
    });

    this->registerFilePathXMLAttribute("iconActivate", [this](std::string value) { this->iconActivate = value; });

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE("style", AutoTabBarStyle, this->setTabStyle,
        {
            {"accent", AutoTabBarStyle::ACCENT},
            {"plain", AutoTabBarStyle::PLAIN},
        });

    this->setFocusSound(brls::SOUND_FOCUS_SIDEBAR);

    this->registerAction(
        "hints/ok"_i18n, brls::BUTTON_A,
        [this](View* view) {
            // une fiche est empilée par-dessus cet onglet : A retourne dans
            // la fiche visible, pas dans le contenu masqué (GONE)
            auto* sidebarBox = this->getParent();
            auto* frame = sidebarBox ? dynamic_cast<AutoTabFrame*>(sidebarBox->getParent()) : nullptr;
            if (this->active && frame && frame->hasDetailView()) {
                brls::Application::giveFocus(frame->getTopDetailView());
                return true;
            }
            if (this->attachedView) brls::Application::giveFocus(this->attachedView);
            return true;
        },
        false, false, brls::SOUND_CLICK_SIDEBAR);

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (this->active) return;

            this->playClickAnimation(status.state != brls::GestureState::UNSURE);

            switch (status.state) {
            case brls::GestureState::UNSURE:
                *soundToPlay = brls::SOUND_FOCUS_SIDEBAR;
                break;
            case brls::GestureState::FAILED:
            case brls::GestureState::INTERRUPTED:
                *soundToPlay = brls::SOUND_TOUCH_UNFOCUS;
                break;
            case brls::GestureState::END:
                *soundToPlay = brls::SOUND_CLICK_SIDEBAR;
                brls::Application::giveFocus(this);
                break;
            default:
                break;
            }
        }));
}

void AutoSidebarItem::setTabStyle(AutoTabBarStyle style) {
    if (style == AutoTabBarStyle::NONE) brls::fatal("SidebarItem style cannot be set to \"None\"");
    if (this->tabStyle != AutoTabBarStyle::NONE) return;

    this->tabStyle = style;
    switch (style) {
    case AutoTabBarStyle::PLAIN:
        this->inflateFromXMLString(autoSidebarItemPlainXML);
        break;
    default:
        this->inflateFromXMLString(autoSidebarItemXML);
    }
}

void AutoSidebarItem::setActive(bool active) {
    if (active == this->active) return;

    brls::Theme theme = brls::Application::getTheme();

    if (active) {
        this->activeEvent.fire(this);
        if (this->tabStyle == AutoTabBarStyle::ACCENT) {
            // en horizontal (pills) l'accent reste GONE : voir applyPillStyle
            if (!this->horizontal) this->accent->setVisibility(brls::Visibility::VISIBLE);
        } else if (this->tabStyle == AutoTabBarStyle::PLAIN) {
            this->setBackgroundColor(this->tabItemActiveBackgroundColor);
        }

        this->label->setTextColor(this->tabItemActiveTextColor);

        if (this->icon->getVisibility() == brls::Visibility::VISIBLE) {
            if (!this->iconActivate.empty())
                this->icon->setImageFromSVGFile(this->iconActivate);
            else if (!this->iconDefault.empty())
                this->icon->setImageFromSVGFile(this->iconDefault);
        }
    } else {
        if (this->tabStyle == AutoTabBarStyle::ACCENT) {
            if (!this->horizontal) this->accent->setVisibility(brls::Visibility::INVISIBLE);
        } else if (this->tabStyle == AutoTabBarStyle::PLAIN) {
            this->setBackgroundColor(this->tabItemBackgroundColor);
        }
        this->label->setTextColor(theme["brls/text"]);

        if (this->icon->getVisibility() == brls::Visibility::VISIBLE && !this->iconDefault.empty())
            this->icon->setImageFromSVGFile(this->iconDefault);
    }

    this->active = active;
    this->applyPillStyle();
}

void AutoSidebarItem::applyPillStyle() {
    if (this->tabStyle != AutoTabBarStyle::ACCENT || !this->horizontal) return;
    auto theme = brls::Application::getTheme();
    if (this->active) {
        // pill active : fond or plein, texte quasi-noir — même duo que les
        // boutons primaires or (recette UI n°5 : le blanc se noyait dans l'or)
        this->setBackgroundColor(theme.getColor("color/app"));
        this->label->setTextColor(theme.getColor("brls/button/primary_enabled_text"));
    } else {
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        this->label->setTextColor(theme.getColor("font/grey"));
    }
}

bool AutoSidebarItem::getHorizontalMode() { return this->horizontal; }

bool AutoSidebarItem::isActive() { return this->active; };

void AutoSidebarItem::onFocusGained() {
    Box::onFocusGained();

    if (this->group) this->group->setActive(this);

    // sidebar verticale : focus = fond or de marque translucide arrondi
    // (le halo borealis est masqué, voir setHorizontalMode)
    if (!this->horizontal && this->tabStyle == AutoTabBarStyle::ACCENT) {
        NVGcolor c = brls::Application::getTheme().getColor("color/app");
        c.a = 0.22f;
        this->setBackgroundColor(c);
    }

    // barre TOP solidaire du scroll : une pill peut gagner le focus pendant
    // que la barre est translatée HORS ÉCRAN (contenu scrollé) → focus
    // invisible, utilisateur perdu (recette console). Ramener le scroll de
    // l'onglet actif à 0 révèle la barre — remonter aux onglets ramène en
    // haut, c'est le comportement attendu.
    if (this->horizontal) {
        for (brls::View* v = this->getParent(); v; v = v->getParent()) {
            if (auto* frame = dynamic_cast<AutoTabFrame*>(v)) {
                if (frame->getActiveTab()) {
                    if (auto* scroller = findScrollingFrame(frame->getActiveTab())) {
                        if (scroller->getContentOffsetY() > 0) scroller->setContentOffsetY(0, true);
                    }
                }
                break;
            }
        }
    }
}

void AutoSidebarItem::onFocusLost() {
    Box::onFocusLost();

    if (!this->horizontal && this->tabStyle == AutoTabBarStyle::ACCENT)
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
}

void AutoSidebarItem::setGroup(AutoSidebarItemGroup* group) {
    this->group = group;

    if (group) group->add(this);
}

brls::GenericEvent* AutoSidebarItem::getActiveEvent() { return &this->activeEvent; }

void AutoSidebarItem::setLabel(std::string text) { this->label->setText(text); }

void AutoSidebarItem::setSubtitle(std::string text) { this->subtitle->setText(text); }

std::string AutoSidebarItem::getLabel() { return this->label->getFullText(); }

brls::View* AutoSidebarItem::AutoSidebarItem::getAttachedView() { return this->attachedView; }

brls::View* AutoSidebarItem::createAttachedView() {
    if (!this->attachedView && this->attachedViewCreator) {
        this->attachedView = this->attachedViewCreator();
        AttachedView* v = dynamic_cast<AttachedView*>(this->attachedView);
        if (v) {
            v->setTabBar(this);
            v->onCreate();
        }
    }
    if (!this->attachedView) {
        brls::fatal("AutoSidebarItem create attached View error");
    }
    this->attachedView->registerAction(
        "hints/back"_i18n, brls::BUTTON_B,
        [this](View* view) {
            if (brls::Application::getInputType() == brls::InputType::TOUCH)
                this->dismiss();
            else
                brls::Application::giveFocus(this);
            return true;
        },
        false, false, brls::SOUND_BACK);
    return this->attachedView;
}

brls::View* AutoSidebarItem::getView(std::string id) {
    View* v = Box::getView(id);
    if (v) return v;
    if (this->attachedView) {
        View* result = this->attachedView->getView(id);
        if (result) return result;
    }
    return nullptr;
}

void AutoSidebarItem::setFontSize(float size) {
    if (this->icon->getVisibility() == brls::Visibility::VISIBLE) {
        size -= 10;
        if (size < 8) size = 8;
    }
    this->label->setFontSize(size);
}

void AutoSidebarItem::setHorizontalMode(bool value) {
    this->horizontal = value;
    if (value) {
        this->setAxis(brls::Axis::COLUMN);
        if (this->tabStyle == AutoTabBarStyle::ACCENT) {
            // onglets horizontaux en « pills » : plus de soulignement accent,
            // l'état actif est porté par un fond or plein (applyPillStyle)
            this->accent->setVisibility(brls::Visibility::GONE);
            this->icon_box->setMarginRight(0);
            this->icon_box->setMarginBottom(0);
            this->setHeight(34);
            this->setCornerRadius(17);
            this->setHighlightCornerRadius(17);
            // le fond du highlight borealis recouvrirait le fond or de la
            // pill active (texte sombre illisible) : on n'en veut pas
            this->setHideHighlightBackground(true);
            this->setPadding(0, 16, 0, 16);
            this->setMargins(0, 5, 0, 5);
            this->applyPillStyle();
        } else {
            this->setPadding(0, 10, 0, 10);
        }
    } else {
        this->setAxis(brls::Axis::ROW);
        if (this->tabStyle == AutoTabBarStyle::ACCENT) {
            // barre accent à droite de l'item (ordre XML), icône 28px centrée
            // (padding 8 + padding 10 de la sidebar = 18, icon_box 30 → centre
            // à 32 = moitié des 64px). Accent (4/0) collé au bord droit : la
            // sidebar n'a plus de paddingRight, zéro pixel d'espacement
            this->accent->setSize(brls::Size(4, View::AUTO));
            this->accent->setMarginTop(9);
            this->accent->setMarginLeft(4);
            this->accent->setMarginRight(0);
            this->icon_box->setMarginBottom(9);
            this->icon_box->setMarginRight(8);
            this->icon_box->setAlignItems(brls::AlignItems::FLEX_START);
            // paddingLeft 18 : la sidebar n'a plus de padding latéral (items
            // pleine largeur), l'icône reste centrée à 18→46 des 64 px
            this->setPadding(0, 0, 0, 18);
            // halo borealis supprimé (gros cadre or disgracieux autour de
            // l'icône) : le focus se signale par un fond or translucide
            // RECTANGLE pleine largeur (onFocusGained/onFocusLost, recette
            // n°6 : pas d'arrondi) ; l'item ACTIF garde icône or + accent
            this->setHideHighlight(true);
            this->setCornerRadius(0);
        } else if (this->tabStyle == AutoTabBarStyle::PLAIN) {
            this->setPadding(8, 0, 8, 0);
            this->setMargins(8, 0, 8, 0);
        }
    }
}

size_t AutoSidebarItem::getCurrentIndex() { return *((size_t*)this->getParentUserData()); }

void AutoSidebarItem::setAttachedViewCreator(TabViewCreator creator) { this->attachedViewCreator = creator; }

AutoSidebarItem::~AutoSidebarItem() {
    brls::Logger::debug("del AutoSidebarItem: {}", this->label->getFullText());
    if (this->attachedView) {
        this->attachedView->setParent(nullptr);
        if (!this->attachedView->isPtrLocked()) {
            delete this->attachedView;
        } else {
            this->attachedView->freeView();
        }
        this->attachedView = nullptr;
    }
}

AutoTabBarStyle AutoSidebarItem::getTabStyle(std::string value) {
    std::unordered_map<std::string, AutoTabBarStyle> enumMap = {
        {"accent", AutoTabBarStyle::ACCENT},
        {"plain", AutoTabBarStyle::PLAIN},
    };
    if (enumMap.count(value) > 0)
        return enumMap[value];
    else
        brls::fatal("Illegal value \"" + value + "\" for AutoSidebarItem attribute \"style\"");
}

void AutoSidebarItem::setDefaultBackgroundColor(NVGcolor c) {
    tabItemBackgroundColor = c;
    this->setBackgroundColor(this->tabItemBackgroundColor);
}

void AutoSidebarItem::setActiveBackgroundColor(NVGcolor c) {
    tabItemActiveBackgroundColor = c;
    if (this->isActive()) {
        this->setBackgroundColor(this->tabItemActiveBackgroundColor);
    }
}

void AutoSidebarItem::setActiveTextColor(NVGcolor c) {
    tabItemActiveTextColor = c;
    this->accent->setColor(c);
    if (this->isActive()) {
        this->label->setTextColor(c);
    }
}

/**
 * auto sidebar group
 */

void AutoSidebarItemGroup::add(AutoSidebarItem* item) { this->items.push_back(item); }

void AutoSidebarItemGroup::setActive(AutoSidebarItem* active) {
    for (AutoSidebarItem* item : this->items) {
        if (item == active)
            item->setActive(true);
        else
            item->setActive(false);
    }
}

void AutoSidebarItemGroup::clear() { this->items.clear(); }

void AutoSidebarItemGroup::removeView(AutoSidebarItem* view) {
    for (auto it = this->items.begin(); it != this->items.end(); it++) {
        if (*it == view) {
            this->items.erase(it);
            break;
        }
    }
}

int AutoSidebarItemGroup::getActiveIndex() {
    for (AutoSidebarItem* item : this->items) {
        if (item->isActive()) return item->getCurrentIndex();
    }
    return -1;
}

/**
 * AttachedView
 */

void AttachedView::setTabBar(AutoSidebarItem* view) { this->tab = view; }
AutoSidebarItem* AttachedView::getTabBar() { return this->tab; }

void AttachedView::onCreate() {}

void AttachedView::registerTabAction(std::string hintText, enum brls::ControllerButton button,
    const brls::BrlsKeyCombination key, brls::ActionListener action, bool hidden, bool allowRepeating,
    enum brls::Sound sound) {
    this->registerAction(hintText, button, action, hidden, allowRepeating, sound);
    if (this->tab) {
        this->tab->registerAction(hintText, button, action, hidden, allowRepeating, sound);
        this->tab->registerAction(key, action, allowRepeating);
    }
}

AttachedView::AttachedView() { this->setGrow(1); }

AttachedView::~AttachedView() { brls::Logger::debug("delete AttachedView"); }