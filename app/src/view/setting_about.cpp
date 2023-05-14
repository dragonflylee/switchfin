#include "view/setting_about.hpp"
#include "utils/config.hpp"

SettingAbout::SettingAbout() {
    this->inflateFromXMLRes("xml/view/setting_about.xml");
    auto& v = AppVersion::instance();
    labelVersion->setText(v.git_tag.empty() ?  "dev-" + v.git_commit : v.git_tag);
    brls::Logger::debug("dialog SettingAbout: create");
}

SettingAbout::~SettingAbout() { brls::Logger::debug("dialog SettingAbout: delete"); }
