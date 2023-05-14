#pragma once

#include <borealis.hpp>

class SettingAbout : public brls::Box {
public:
    SettingAbout();

    ~SettingAbout();

private:
    BRLS_BIND(brls::Label, labelVersion, "setting/about/version");
};