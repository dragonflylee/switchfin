/*
    GMCA — libraries manager (see library_manager.hpp).
*/

#include "view/library_manager.hpp"
#include "view/svg_image.hpp"

using namespace brls::literals;  // for _i18n

namespace {

/// linear RGBA blend (t=0 -> a, t=1 -> b).
NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    NVGcolor c;
    for (int i = 0; i < 4; i++) c.rgba[i] = a.rgba[i] * (1.f - t) + b.rgba[i] * t;
    return c;
}

/// shell of the manager screen: title + description + a scrollable, centered
/// column of rows (populated programmatically).
const std::string managerXML = R"xml(
    <brls:Box
        axis="column"
        grow="1"
        justifyContent="flexStart"
        alignItems="center"
        paddingTop="40"
        paddingLeft="30"
        paddingRight="30"
        paddingBottom="20">

        <brls:Label
            fontSize="30"
            text="@i18n/main/setting/libraries/header"
            marginBottom="6" />

        <brls:Label
            id="lib_manager/desc"
            fontSize="15"
            horizontalAlign="center"
            text="@i18n/main/setting/libraries/description"
            marginBottom="24" />

        <brls:ScrollingFrame
            width="100%"
            grow="1">
            <brls:Box
                width="100%"
                axis="column"
                alignItems="center"
                paddingTop="4"
                paddingBottom="20">
                <brls:Box
                    id="lib_manager/rows"
                    width="680"
                    axis="column" />
            </brls:Box>
        </brls:ScrollingFrame>

    </brls:Box>
)xml";

/// One reorderable row: icon + name + a visible/hidden tag. Fully custom focus
/// / grab visuals (the Borealis highlight is hidden), mirroring ConnectionTile.
class LibraryRow : public brls::Box {
public:
    LibraryRow(LibraryManager* mgr, int index, const MainTabFrame::SidebarEntry& e)
        : mgr(mgr), index(index), shown(e.visible) {
        auto theme = brls::Application::getTheme();
        this->accent = theme.getColor("color/app");
        this->surface = theme.getColor("color/surface");
        this->text = theme.getColor("brls/text");
        this->grey = theme.getColor("font/grey");

        this->setAxis(brls::Axis::ROW);
        this->setWidthPercentage(100);
        this->setHeight(60);
        this->setCornerRadius(12);
        this->setMarginBottom(8);
        this->setPaddingLeft(18);
        this->setPaddingRight(18);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setFocusable(true);
        this->setHideHighlight(true);

        this->icon = new SVGImage();
        this->icon->setWidth(24);
        this->icon->setHeight(24);
        this->icon->setMarginRight(16);
        std::string path = e.icon;
        const std::string prefix = "@res/";
        if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
        this->icon->setImageFromSVGRes(path);
        this->addView(this->icon);

        this->label = new brls::Label();
        this->label->setText(e.label);
        this->label->setFontSize(18);
        this->label->setGrow(1.0f);
        this->addView(this->label);

        this->tag = new brls::Label();
        this->tag->setText(this->shown ? "main/setting/libraries/visible"_i18n
                                        : "main/setting/libraries/hidden"_i18n);
        this->tag->setFontSize(14);
        this->addView(this->tag);

        bool grabbed = mgr->isGrabbed(index);

        // A: grab / drop
        this->registerAction(
            grabbed ? "main/setting/libraries/drop"_i18n : "main/setting/libraries/grab"_i18n, brls::BUTTON_A,
            [mgr, index](brls::View*) {
                mgr->toggleGrab(index);
                return true;
            });

        // Y: show / hide (ignored while a row is grabbed)
        this->registerAction("main/setting/libraries/toggle"_i18n, brls::BUTTON_Y, [mgr, index](brls::View*) {
            if (mgr->anyGrabbed()) return true;  // swallow: no visibility change mid-move
            mgr->toggleVisible(index);
            return true;
        });

        // D-pad up/down: move while grabbed (consumes navigation), otherwise
        // let the focus navigate normally (return false)
        this->registerAction(
            "", brls::BUTTON_NAV_UP,
            [mgr, index](brls::View*) {
                if (!mgr->isGrabbed(index)) return false;
                mgr->moveGrabbed(-1);
                return true;
            },
            true, true);
        this->registerAction(
            "", brls::BUTTON_NAV_DOWN,
            [mgr, index](brls::View*) {
                if (!mgr->isGrabbed(index)) return false;
                mgr->moveGrabbed(1);
                return true;
            },
            true, true);

        // mouse / touch: a tap toggles visibility (Y is gamepad-only, so this
        // is the show/hide affordance for pointer input). Ignored mid-grab.
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this, [mgr, index]() {
            if (mgr->anyGrabbed()) return;
            mgr->toggleVisible(index);
        }));

        this->applyVisual(false);
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->applyVisual(true);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->applyVisual(false);
    }

private:
    void applyVisual(bool focused) {
        bool grabbed = this->mgr->isGrabbed(this->index);
        bool lift = focused || grabbed;

        float tint = grabbed ? 0.30f : (focused ? 0.18f : 0.f);
        this->setBackgroundColor(tint > 0 ? mix(this->surface, this->accent, tint) : nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(this->accent);
        this->setBorderThickness(grabbed ? 3.f : (focused ? 2.f : 0.f));
        this->setShadowType(lift ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
        this->setShadowVisibility(lift);

        // dim hidden rows at rest so the state reads at a glance; keep them
        // fully legible while focused/grabbed
        this->setAlpha((!this->shown && !lift) ? 0.45f : 1.f);

        this->label->setTextColor(lift ? this->accent : this->text);
        this->tag->setTextColor(this->shown ? this->accent : this->grey);
    }

    LibraryManager* mgr;
    int index;
    bool shown;

    NVGcolor accent {}, surface {}, text {}, grey {};
    SVGImage* icon = nullptr;
    brls::Label* label = nullptr;
    brls::Label* tag = nullptr;
};

}  // namespace

LibraryManager::LibraryManager(MainTabFrame* frame) : frame(frame) {
    this->inflateFromXMLString(managerXML);
    this->entries = frame->getReorderableEntries();

    this->rowsBox = dynamic_cast<brls::Box*>(this->getView("lib_manager/rows"));
    if (auto* desc = dynamic_cast<brls::Label*>(this->getView("lib_manager/desc")))
        desc->setTextColor(brls::Application::getTheme().getColor("font/grey"));

    this->rebuild();
    this->live = true;
    brls::Logger::debug("LibraryManager: create ({} entries)", this->entries.size());
}

void LibraryManager::rebuild() {
    if (!this->rowsBox) return;
    this->rowsBox->clearViews();
    this->focusTarget = nullptr;

    if (this->entries.empty()) {
        auto* empty = new brls::Label();
        empty->setText("main/setting/libraries/empty"_i18n);
        empty->setFontSize(16);
        empty->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        empty->setMarginTop(40);
        this->rowsBox->addView(empty);
        return;
    }

    for (int i = 0; i < (int)this->entries.size(); i++)
        this->rowsBox->addView(new LibraryRow(this, i, this->entries[i]));

    int idx = this->pendingFocus;
    if (idx < 0) idx = 0;
    if (idx >= (int)this->entries.size()) idx = (int)this->entries.size() - 1;
    this->focusTarget = this->rowsBox->getChildren()[idx];

    if (this->live && this->focusTarget) brls::Application::giveFocus(this->focusTarget);
}

void LibraryManager::commit() {
    std::vector<std::string> order;
    std::set<std::string> hidden;
    order.reserve(this->entries.size());
    for (auto& e : this->entries) {
        order.push_back(e.id);
        if (!e.visible) hidden.insert(e.id);
    }
    this->frame->setSidebarLayout(order, hidden);
}

void LibraryManager::toggleGrab(int index) {
    this->grabbedIndex = (this->grabbedIndex == index) ? -1 : index;
    this->pendingFocus = index;
    this->rebuild();
}

void LibraryManager::moveGrabbed(int delta) {
    if (this->grabbedIndex < 0) return;
    int j = this->grabbedIndex + delta;
    if (j < 0 || j >= (int)this->entries.size()) return;

    std::swap(this->entries[this->grabbedIndex], this->entries[j]);
    this->grabbedIndex = j;
    this->pendingFocus = j;

    this->commit();   // persist + reflect in the sidebar live
    this->rebuild();  // rebuild the manager rows + refocus the moved row
}

void LibraryManager::toggleVisible(int index) {
    if (index < 0 || index >= (int)this->entries.size()) return;
    this->entries[index].visible = !this->entries[index].visible;
    this->pendingFocus = index;

    this->commit();
    this->rebuild();
}

brls::View* LibraryManager::getDefaultFocus() {
    return this->focusTarget ? this->focusTarget : brls::Box::getDefaultFocus();
}
