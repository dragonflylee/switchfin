/*
    GMCA — button with icon + label (brls::Button does not support an icon).
    Styles: "primary" (gold background, dark text) and "bordered" (outline).
    XML attributes: icon (@res/...), text (@i18n/...), buttonStyle.
*/

#pragma once

#include <borealis.hpp>

class SVGImage;

class IconButton : public brls::Box {
public:
    IconButton();

    void setIcon(const std::string& res);
    void setText(const std::string& text);
    void setButtonStyle(const std::string& style);
    /// Muted = visually disabled (dim border + grey text) but STILL focusable, so
    /// a D-pad user can land on it and trigger the "why is this unavailable" help
    /// (the alternative — hiding it — strands the focus highlight on a gone view).
    void setMuted(bool muted);

    static brls::View* create();

private:
    BRLS_BIND(SVGImage, icon, "icon_button/icon");
    BRLS_BIND(brls::Label, label, "icon_button/label");

    void applyStyle();

    std::string styleName = "bordered";
    bool muted = false;
};
