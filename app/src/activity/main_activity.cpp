#include "activity/main_activity.hpp"
#include "utils/config.hpp"

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity: create");

    AppConfig::instance().checkDanmuku();
}

void MainActivity::onContentAvailable() {
    if (AppConfig::instance().getRemotes().empty()) {
        // Hide the remote tab if there are no remotes
        brls::View* tab = this->getView("tab/remote");
        tab->setVisibility(brls::Visibility::GONE);
    }
}