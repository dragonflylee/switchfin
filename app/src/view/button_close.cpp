//
// Created by fang on 2022/12/27.
//

#include "view/button_close.hpp"
#include "view/auto_tab_frame.hpp"

ButtonClose::ButtonClose() {
    this->inflateFromXMLRes("xml/view/button_close.xml");
    brls::Logger::debug("View ButtonClose: create");

    this->registerColorXMLAttribute("textColor", [this](NVGcolor value) { this->setTextColor(value); });

    this->registerClickAction([this](...) {
        // fiche empilée dans le tab frame : la croix dépile la fiche au lieu
        // de dépiler l'AppletFrame (qui afficherait « Quitter ? »)
        if (!ui::popDetail(this)) this->dismiss();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void ButtonClose::setTextColor(NVGcolor color) { this->label->setTextColor(color); }

ButtonClose::~ButtonClose() { brls::Logger::debug("View ButtonClose: delete"); }

brls::View* ButtonClose::create() { return new ButtonClose(); }