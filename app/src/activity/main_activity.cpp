#include "activity/main_activity.hpp"
#include "utils/config.hpp"

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity: create");

    AppConfig::instance().checkDanmuku();
}