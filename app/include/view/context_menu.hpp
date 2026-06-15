#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>

class SVGImage;
class BaseCardCell;

/// Context menu entry: icon + label + state check mark (selected).
class MenuItem : public brls::Box {
public:
    MenuItem();

    void setIcon(const std::string& res);
    void setTitle(const std::string& text);
    void setSelected(bool selected);
    bool getSelected() const { return this->selected; }

    static brls::View* create();

    BRLS_BIND(brls::Label, title, "menu_item/title");

private:
    BRLS_BIND(SVGImage, icon, "menu_item/icon");
    BRLS_BIND(SVGImage, check, "menu_item/check");

    bool selected = false;
};

class ContextMenu : public brls::Box {
public:
    ContextMenu(const jellyfin::Item& item, BaseCardCell* view = nullptr);

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->context->getDefaultFocus(); }

private:
    BRLS_BIND(brls::Box, context, "video/context/menu");
    BRLS_BIND(brls::Box, cancel, "video/cancel");

    BRLS_BIND(brls::Label, labelTitle, "menu/title");
    BRLS_BIND(MenuItem, btnFavorite, "menu/favorite");
    BRLS_BIND(MenuItem, btnMarkPlay, "menu/mark/play");
    BRLS_BIND(MenuItem, btnDownload, "menu/download");

    bool doPlayed();
    bool doFavorite();
    bool unPlayed();
    bool unFavorite();

    std::string itemId;
    BaseCardCell* cell = nullptr;
};