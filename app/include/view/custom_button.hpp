//
// Created by fang on 2022/8/22.
//

// register this view in main.cpp
//#include "view/custom_button.hpp"
//    brls::Application::registerXMLView("CustomButton", CustomButton::create);

#pragma once

#include <borealis.hpp>

class SVGImage;

class CustomButton : public brls::Box {
public:
    CustomButton();

    ~CustomButton();

    static View *create();

    void onFocusGained() override;

    void onFocusLost() override;

    void subscribe(std::function<void(bool)> cb);

    View *getNextFocus(brls::FocusDirection direction, View *currentView) override;

    void setCustomNavigation(std::function<brls::View *(brls::FocusDirection)> navigation);

private:
    std::string iconDefault, iconActivate;
    SVGImage *icon = nullptr;
    brls::Event<bool> focusEvent;
    std::function<brls::View *(brls::FocusDirection)> customNavigation = nullptr;
};