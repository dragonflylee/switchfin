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

/// true if `view` belongs to the parent chain leading to `root`
/// (both pointers are alive: safe upward walk)
static bool isDescendantOf(brls::View* view, brls::View* root) {
    for (brls::View* v = view; v; v = v->getParent()) {
        if (v == root) return true;
    }
    return false;
}

/// first brls::ScrollingFrame of the subtree (DFS): the grid/list whose
/// scrolling the TOP tab bar follows (RecyclingGrid inherits from it).
/// nullptr if the tab does not scroll -> bar fixed at 0.
static brls::ScrollingFrame* findScrollingFrame(brls::View* root) {
    if (auto* frame = dynamic_cast<brls::ScrollingFrame*>(root)) return frame;
    if (auto* box = dynamic_cast<brls::Box*>(root)) {
        for (auto* child : box->getChildren()) {
            if (auto* frame = findScrollingFrame(child)) return frame;
        }
    }
    return nullptr;
}

/// true if `needle` is in the LIVE subtree of `root`. `needle` is NEVER
/// dereferenced (address comparison): this is the revalidation of a
/// potentially dead focus pointer before restoration.
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

    this->registerColorXMLAttribute("tabBackgroundColor", [this](NVGcolor value) {
        // In vertical mode `sidebar` is only the scrollable tab list (the scroll
        // frame's content view), so its background covers just the tabs' height.
        // The full-height holder column must carry the color so the footer and
        // the empty scroll area stay filled. Stored so buildVerticalSidebar can
        // apply it whatever the XML attribute order.
        this->sidebarBackgroundColor = value;
        this->sidebar->setBackgroundColor(value);
        if (this->sidebarHolder && this->sidebarHolder != this->sidebar)
            this->sidebarHolder->setBackgroundColor(value);
    });

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
    // side paddings 0: full-width items (the focus background covers the
    // sidebar edge to edge); the accent (marginRight 0) is flush with the
    // right edge — zero pixels of spacing. Tight top/bottom (14): Home sits
    // high, the avatar + settings sit low, against the sidebar's vertical edges.
    this->sidebar->setPadding(14, 0, 14, 0);

    // horizontal / top mode default: the tab bar itself is the holder.
    // setSideBarPosition(LEFT/RIGHT) rewrites this into a scroll+footer column.
    this->sidebarHolder = this->sidebar;
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
        // FLOATING bar: out of the flow (absolute), full width, transparent
        // background (the top XMLs set none) — the content occupies the
        // whole frame and scrolls beneath (internal paddingTop ~70 on the
        // grid side); drawn last in draw() to stay on top
        this->sidebar->setPositionType(brls::PositionType::ABSOLUTE);
        this->sidebar->setPositionTop(0);
        this->sidebar->setPositionLeft(0);
        this->sidebar->setWidthPercentage(100);
        break;
    case AutoTabBarPosition::RIGHT:
        this->setAxis(brls::Axis::ROW);
        this->setDirection(brls::Direction::RIGHT_TO_LEFT);
        this->setHorizontalMode(false);
        this->buildVerticalSidebar();
        break;
    case AutoTabBarPosition::LEFT:
        this->setAxis(brls::Axis::ROW);
        this->setDirection(brls::Direction::LEFT_TO_RIGHT);
        this->setHorizontalMode(false);
        this->buildVerticalSidebar();
        break;
    default:;
    }
    this->invalidate();
}

int AutoTabFrame::getActiveIndex() { return this->group.getActiveIndex(); }

brls::GenericEvent::Callback AutoTabFrame::makeTabSwitchCallback() {
    return [this](brls::View* view) {
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
    };
}

void AutoTabFrame::buildVerticalSidebar() {
    // Built once, from the first setSideBarPosition(LEFT/RIGHT). "Only set once"
    // per the header, but stay idempotent: just refresh the width on re-entry.
    if (this->sidebarScroll) {
        this->sidebarHolder->setWidth(this->sidebarWidth);
        return;
    }

    // A vertical sidebar is a scrollable tab list stacked on top of a fixed
    // footer (avatar, settings, network status). When the tabs no longer fit
    // (many libraries), the LIST scrolls while the footer stays pinned at the
    // bottom — the avatar and gear are always reachable. Previously the whole
    // bar was a single Box with a grow spacer, so extra tabs pushed the avatar
    // off-screen.
    auto* column = new brls::Box(brls::Axis::COLUMN);
    column->setWidth(this->sidebarWidth);
    // full-height background so the footer + empty scroll area keep the sidebar
    // color (the scroll content view only covers the tabs' own height)
    column->setBackgroundColor(this->sidebarBackgroundColor);

    this->sidebarScroll = new brls::ScrollingFrame();
    // CENTERED: keeps the focused tab centered while scrolling and, crucially,
    // hands focus down to the footer once scrolled to the bottom (see the
    // ScrollingFrame::getNextFocus delegation). Same behavior as brls::Sidebar.
    this->sidebarScroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    this->sidebarScroll->setScrollingIndicatorVisible(false);
    this->sidebarScroll->setGrow(1.0f);

    this->sidebarFooter = new brls::Box(brls::Axis::COLUMN);
    // 14px bottom inset mirrors the old sidebar's bottom padding (status row).
    this->sidebarFooter->setPadding(0, 0, 14, 0);

    // move the tab list into the scroll frame, keeping it (and its items) alive
    this->removeView(this->sidebar, false);
    this->sidebarScroll->setContentView(this->sidebar);

    column->addView(this->sidebarScroll);
    column->addView(this->sidebarFooter);
    // replaces `sidebar` as the frame's first child (index 0, before content)
    this->addView(column, 0);
    this->sidebarHolder = column;
}

void AutoTabFrame::addTab(AutoSidebarItem* tab, TabViewCreator creator) {
    this->addTab(tab, std::move(creator), this->sidebar->getChildren().size());
}

void AutoTabFrame::addTab(AutoSidebarItem* tab, TabViewCreator creator, size_t position) {
    tab->setDefaultBackgroundColor(this->tabItemBackgroundColor);
    tab->setActiveBackgroundColor(this->tabItemActiveBackgroundColor);
    tab->setActiveTextColor(this->tabItemActiveTextColor);

    this->addItem(tab, std::move(creator), this->makeTabSwitchCallback(), position);
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
    // Cycle across the full visual order (scrollable list + pinned footer), so
    // LB/RB reaches the avatar and settings too — the old grow spacer used to
    // sit between them and swallowed the jump.
    std::vector<AutoSidebarItem*> seq = this->navSequence();
    if (seq.empty()) return;

    int currentIndex = -1;
    for (size_t i = 0; i < seq.size(); i++)
        if (seq[i]->isActive()) {
            currentIndex = (int)i;
            break;
        }

    if (currentIndex < 0) {
        brls::Application::giveFocus(seq[0]);
    } else if (seq.size() == 1) {
        // shake highlight (currentFocus can be null during a destruction)
        brls::View* focus = brls::Application::getCurrentFocus();
        if (focus) focus->shakeHighlight(this->isHorizontal ? brls::FocusDirection::RIGHT : brls::FocusDirection::DOWN);
    } else {
        brls::Application::giveFocus(seq[(currentIndex + 1) % seq.size()]);
    }
}

void AutoTabFrame::focus2LastTab() {
    std::vector<AutoSidebarItem*> seq = this->navSequence();
    if (seq.empty()) return;

    int currentIndex = -1;
    for (size_t i = 0; i < seq.size(); i++)
        if (seq[i]->isActive()) {
            currentIndex = (int)i;
            break;
        }

    if (currentIndex < 0) {
        brls::Application::giveFocus(seq[0]);
    } else if (seq.size() == 1) {
        // shake highlight (currentFocus can be null during a destruction)
        brls::View* focus = brls::Application::getCurrentFocus();
        if (focus) focus->shakeHighlight(this->isHorizontal ? brls::FocusDirection::LEFT : brls::FocusDirection::UP);
    } else {
        brls::Application::giveFocus(seq[(currentIndex - 1 + (int)seq.size()) % (int)seq.size()]);
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

void AutoTabFrame::removeTabById(const std::string& id) {
    AutoSidebarItem* item = dynamic_cast<AutoSidebarItem*>(this->sidebar->getView(id));
    if (!item) return;
    if (item->isFocused()) {
        this->setLastFocusedView(nullptr);
        brls::Application::giveFocus(this);
    }
    this->sidebar->removeView(item, true);
    this->group.removeView(item);
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
    // detach the stacked detail pages (freed via the deletionPool)
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
    // a tab change abandons the detail stack: the tab content becomes
    // the only view in the content area again
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
    // focus inside a recycler: remember (recycler, index) for an
    // INDEX-based restore at pop (cell pointers get rebound to other
    // media while the content is hidden)
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

    this->addView(detail);  // calls willAppear
    // hidden WITHOUT being destroyed: state (scroll, data, inner focus) intact
    if (covered) covered->setVisibility(brls::Visibility::GONE);
    this->detailStack.push_back(entry);

    // the focus may not resolve for several frames: a freshly pushed
    // detail sometimes has NOTHING focusable until its request has
    // answered (skeletons — e.g. "go to season").
    this->retryDetailFocus(detail, 600);
}

/// Gives focus to the detail at the top of the stack, retrying every
/// frame while nothing in it is focusable (async content). While
/// waiting, the focus is placed on the SIDEBAR: leaving it on the
/// covered (GONE) view drew a ghost halo with a degenerate frame and
/// navigated a hidden tree, up to a segfault on recycled/destroyed
/// cells.
void AutoTabFrame::retryDetailFocus(brls::View* detail, int attemptsLeft) {
    // detail popped (quick B) or covered in the meantime: steal nothing
    if (this->detailStack.empty() || this->detailStack.back().view != detail) return;
    brls::View* focus = brls::Application::getCurrentFocus();
    if (focus && isDescendantOf(focus, detail)) return;  // resolved
    brls::Application::giveFocus(detail);
    focus = brls::Application::getCurrentFocus();
    if (focus && isDescendantOf(focus, detail)) return;  // resolved
    if (!focus || !isDescendantOf(focus, this->sidebar)) brls::Application::giveFocus(this->sidebar);
    if (attemptsLeft <= 0) return;  // healthy state (sidebar), stop here
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

    // detaches the page; freeView (via removeView) defers destruction to
    // end of frame (deletionPool) — safe even from this page's own B action
    this->removeView(entry.view, true);

    // focus in a recycler at push time: INDEX-based restore on the next
    // frame (lets the relayout of the unmasked content — and its possible
    // reloadData — happen first), via selectRowAt which scrolls and
    // re-materializes the right cell
    if (uncovered && entry.previousRecycler && treeContains(uncovered, entry.previousRecycler)) {
        brls::View* recycler = entry.previousRecycler;
        size_t index = entry.previousIndex;
        brls::Application::giveFocus(uncovered);
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN, recycler, index]() {
            ASYNC_RELEASE
            // the recycler lives in the uncovered tab/page: it cannot have
            // disappeared in one frame without user interaction
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

    // otherwise: restores the remembered pointer only if it still lives in
    // the uncovered content; else the container resolves its default focus
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
        this->removeView(it->view, true);  // deferred destruction (deletionPool)
    }
    this->detailStack.clear();
    if (this->activeTab) this->activeTab->setVisibility(brls::Visibility::VISIBLE);
}

void ui::presentDetail(brls::View* from, brls::View* detail) {
    // outermost AutoTabFrame: ignores nested tabs (the libraries'
    // horizontal tabs) to cover the whole content area
    AutoTabFrame* frame = nullptr;
    for (brls::View* v = from; v; v = v->getParent()) {
        if (auto* f = dynamic_cast<AutoTabFrame*>(v)) frame = f;
    }
    if (frame) {
        frame->pushDetailView(detail);
    } else if (from) {
        // outside the sidebar (presented search, server list...)
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
    // Do not navigate down, except through sidebar area. The direct child that
    // holds the tab bar is `sidebarHolder` (== sidebar in horizontal mode, ==
    // the scroll+footer column in vertical mode), so key off that.
    if (direction == brls::FocusDirection::DOWN && currentView != this->sidebarHolder) {
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
        // GONE views (tab content covered by a detail page, pages buried
        // under the stack) must never receive focus:
        // isFocusable() only checks the leaf's visibility, not its
        // ancestors'
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
        // 34px pills centered in the bar (tabHeight 60)
        this->sidebar->setAlignItems(brls::AlignItems::CENTER);
    } else {
        this->sidebar->setPadding(14, 0, 14, 0);
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

void AutoTabFrame::addFooterTab(AutoSidebarItem* tab, TabViewCreator creator) {
    // no footer (horizontal / top mode): behave like a normal tab
    if (!this->sidebarFooter) {
        this->addTab(tab, std::move(creator));
        return;
    }
    // same wiring as addTab (colors + group + content-switch on focus), but the
    // item lands in the pinned footer instead of the scrollable tab list.
    tab->setDefaultBackgroundColor(this->tabItemBackgroundColor);
    tab->setActiveBackgroundColor(this->tabItemActiveBackgroundColor);
    tab->setActiveTextColor(this->tabItemActiveTextColor);
    tab->setAttachedViewCreator(std::move(creator));
    tab->setHorizontalMode(this->isHorizontal);
    tab->setGroup(&this->group);
    tab->getActiveEvent()->subscribe(this->makeTabSwitchCallback());
    this->sidebarFooter->addView(tab);
}

void AutoTabFrame::clearItems() {
    this->setTabAttachedView(nullptr);
    this->sidebar->clearViews();
    if (this->sidebarFooter) this->sidebarFooter->clearViews();
    this->group.clear();
    this->setLastFocusedView(nullptr);
}

brls::Box* AutoTabFrame::getSidebar() { return this->sidebar; }

brls::Box* AutoTabFrame::getSidebarFooter() { return this->sidebarFooter; }

std::vector<AutoSidebarItem*> AutoTabFrame::navSequence() {
    std::vector<AutoSidebarItem*> seq;
    for (auto* v : this->sidebar->getChildren())
        if (auto* item = dynamic_cast<AutoSidebarItem*>(v)) seq.push_back(item);
    if (this->sidebarFooter)
        for (auto* v : this->sidebarFooter->getChildren())
            if (auto* item = dynamic_cast<AutoSidebarItem*>(v)) seq.push_back(item);
    return seq;
}

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
        // bar follows the scroll: translation of -offset of the first
        // ScrollingFrame of the active tab — scroll, and the bar exits
        // through the top; back to 0, it returns (same mechanism as the
        // ScrollingFrame's own contentView->setTranslationY(-offset)).
        // Resolved on EVERY frame, no pointer cache: tab contents get
        // (re)built asynchronously (data received, reloads) and a retained
        // pointer would dangle; the DFS stops at the first grid (1-2
        // levels), negligible cost.
        // The translation shifts getFrame() of the bar and its children ->
        // the hitTest below follows for free: bar off-screen, taps reach
        // the grid again. The paddingTop 70 inside the contents stay
        // (resting position under the bar). Tab change: the bar jumps to
        // the new grid's offset (accepted).
        float scrollOffset = 0.0f;
        if (this->activeTab) {
            if (auto* scroller = findScrollingFrame(this->activeTab))
                scrollOffset = std::max(0.0f, scroller->getContentOffsetY());
        }
        this->sidebar->setTranslationY(-scrollOffset);

        // floating bar: the content scrolls BENEATH the bar, so it must be
        // painted LAST — the child order (sidebar first, essential to the
        // navigation indices of getNextFocus) would give the opposite via
        // Box::draw. frame() handles visibility and alpha; the culling test
        // of Box::draw only applies to non-Box leaves, never to these
        // children (sidebar and contents are Boxes)
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

    // touch mirror of the floating draw order: Box::hitTest walks the
    // children in REVERSE order (the last drawn wins), but the bar is
    // child 0 — the full-height content would capture all taps in the
    // bar area. So probe the bar first.
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

            <!-- 28px: centered in the 64 sidebar (left padding 10 + 8,
                 icon_box 30 wide) without clipping or overflow onto the accent -->
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
            // a detail page is stacked on top of this tab: A goes back into
            // the visible page, not into the hidden (GONE) content
            auto* sidebarBox = this->getParent();
            auto* frame = sidebarBox ? dynamic_cast<AutoTabFrame*>(sidebarBox->getParent()) : nullptr;
            if (this->active && frame && frame->hasDetailView()) {
                // re-selecting the active tab returns to its root (library
                // grid), popping any stacked detail page (fiche)
                frame->clearDetailViews();
            }
            if (this->attachedView) brls::Application::giveFocus(this->attachedView);
            return true;
        },
        false, false, brls::SOUND_CLICK_SIDEBAR);

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (this->active) {
                // clicking the already-active tab returns to its root: pop any
                // stacked detail page (fiche) back to the library grid
                if (status.state == brls::GestureState::END) {
                    auto* sidebarBox = this->getParent();
                    auto* frame = sidebarBox ? dynamic_cast<AutoTabFrame*>(sidebarBox->getParent()) : nullptr;
                    if (frame && frame->hasDetailView()) {
                        *soundToPlay = brls::SOUND_CLICK_SIDEBAR;
                        frame->clearDetailViews();
                        if (this->attachedView) brls::Application::giveFocus(this->attachedView);
                    }
                }
                return;
            }

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
            // in horizontal mode (pills) the accent stays GONE: see applyPillStyle
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
        // active pill: solid gold background, near-black text — same duo as
        // the gold primary buttons (white drowned in the gold)
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

    // vertical sidebar: focus = rounded translucent brand-gold background
    // (the borealis halo is hidden, see setHorizontalMode)
    if (!this->horizontal && this->tabStyle == AutoTabBarStyle::ACCENT) {
        NVGcolor c = brls::Application::getTheme().getColor("color/app");
        c.a = 0.22f;
        this->setBackgroundColor(c);
    }

    // scroll-following TOP bar: a pill can gain focus while the bar is
    // translated OFF-SCREEN (content scrolled) -> invisible focus, lost
    // user. Bringing the active tab's scroll back to 0 reveals the bar —
    // going back up to the tabs scrolls to the top, which is the
    // expected behavior.
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
            // horizontal tabs as "pills": no more accent underline,
            // the active state is carried by a solid gold background (applyPillStyle)
            this->accent->setVisibility(brls::Visibility::GONE);
            this->icon_box->setMarginRight(0);
            this->icon_box->setMarginBottom(0);
            this->setHeight(34);
            this->setCornerRadius(17);
            this->setHighlightCornerRadius(17);
            // the borealis highlight background would cover the active
            // pill's gold background (unreadable dark text): we don't want it
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
            // accent bar on the right of the item (XML order), 28px icon
            // centered (padding 8 + sidebar padding 10 = 18, icon_box 30 ->
            // center at 32 = half of the 64px). Accent (4/0) flush with the
            // right edge: the sidebar has no paddingRight anymore, zero
            // pixels of spacing
            this->accent->setSize(brls::Size(4, View::AUTO));
            this->accent->setMarginTop(9);
            this->accent->setMarginLeft(4);
            this->accent->setMarginRight(0);
            this->icon_box->setMarginBottom(9);
            this->icon_box->setMarginRight(8);
            this->icon_box->setAlignItems(brls::AlignItems::FLEX_START);
            // paddingLeft 18: the sidebar has no side padding anymore
            // (full-width items), the icon stays centered at 18->46 of the 64 px
            this->setPadding(0, 0, 0, 18);
            // borealis halo removed (ugly big gold frame around the icon):
            // focus is signaled by a full-width translucent gold
            // RECTANGLE background (onFocusGained/onFocusLost, no
            // rounding); the ACTIVE item keeps gold icon + accent
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