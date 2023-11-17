//
// Created by fang on 2022/8/22.
//

#include "view/custom_button.hpp"
#include "view/svg_image.hpp"

CustomButton::CustomButton() {
    brls::Logger::debug("View CustomButton: create");
    this->icon = new SVGImage();
    this->addView(this->icon);

    this->registerFilePathXMLAttribute("icon", [this](std::string value) {
        this->iconDefault = value;
        this->icon->setVisibility(brls::Visibility::VISIBLE);
        this->icon->setImageFromSVGFile(value);
    });
    this->registerFilePathXMLAttribute("iconActivate", [this](std::string value) { this->iconActivate = value; });
}

CustomButton::~CustomButton() { brls::Logger::debug("View CustomButton: delete"); }

brls::View* CustomButton::create() { return new CustomButton(); }

void CustomButton::onFocusLost() {
    this->focusEvent.fire(false);
    this->icon->setImageFromSVGFile(this->iconDefault);
    Box::onFocusLost();
}

void CustomButton::onFocusGained() {
    this->focusEvent.fire(true);
    this->icon->setImageFromSVGFile(this->iconActivate);
    Box::onFocusGained();
}

brls::Event<bool>* CustomButton::getFocusEvent() { return &this->focusEvent; }

brls::View* CustomButton::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    brls::View* next = nullptr;
    if (this->customNavigation) next = this->customNavigation(direction);
    if (!next) next = Box::getNextFocus(direction, currentView);
    return next;
}

void CustomButton::setCustomNavigation(std::function<brls::View*(brls::FocusDirection)> navigation) {
    this->customNavigation = navigation;
}
