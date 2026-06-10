/*
    pleNx — bouton avec icône + libellé (brls::Button ne supporte pas d'icône).
    Styles : "primary" (fond or, texte sombre) et "bordered" (contour).
    Attributs XML : icon (@res/...), text (@i18n/...), buttonStyle.
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
