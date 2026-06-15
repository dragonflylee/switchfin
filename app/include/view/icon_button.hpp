/*
    pleNx — button with icon + label (brls::Button does not support an icon).
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

    static brls::View* create();

private:
    BRLS_BIND(SVGImage, icon, "icon_button/icon");
    BRLS_BIND(brls::Label, label, "icon_button/label");

    void applyStyle();

    std::string styleName = "bordered";
};
