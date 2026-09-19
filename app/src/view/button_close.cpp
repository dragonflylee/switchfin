//
// Created by fang on 2022/12/27.
//

#include "view/button_close.hpp"

ButtonClose::ButtonClose() {
    this->inflateFromXMLRes("xml/view/button_close.xml");
#ifdef PS5_NATIVE_GPU
    this->setHideHighlightBackground(true);
#endif
    brls::Logger::debug("View ButtonClose: create");

    this->registerColorXMLAttribute("textColor", [this](NVGcolor value) { this->setTextColor(value); });

    this->registerClickAction([this](...) {
        this->dismiss();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void ButtonClose::setTextColor(NVGcolor color) { this->label->setTextColor(color); }

ButtonClose::~ButtonClose() { brls::Logger::debug("View ButtonClose: delete"); }

#ifdef PS5_NATIVE_GPU
brls::View* ButtonClose::create() { return new ButtonClose(); }
#else
brls::View* ButtonClose::create() { return new ButtonClose(); }
#endif
