#pragma once

#include <borealis.hpp>

class ButtonClose;

class ContextMenu : public brls::Box {
public:
    ContextMenu(const std::string& itemId);

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->context->getDefaultFocus(); }

private:
    BRLS_BIND(brls::ScrollingFrame, context, "video/context/menu");
    BRLS_BIND(brls::Box, cancel, "video/cancel");

    BRLS_BIND(brls::RadioCell, btnFavorite, "menu/favorite");
    BRLS_BIND(brls::RadioCell, btnMarkPlay, "menu/mark/play");

    bool onClick();
};