#include "view/setting_about.hpp"
#include "utils/config.hpp"

SettingAbout::SettingAbout() {
    this->inflateFromXMLRes("xml/view/setting_about.xml");
    labelVersion->setText(AppVersion::getVersion());
    brls::Logger::debug("dialog SettingAbout: create");
}

SettingAbout::~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }
