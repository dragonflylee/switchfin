#include "activity/main_activity.hpp"
#include "utils/config.hpp"
#include "api/http.hpp"

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity: create");

    auto& conf = AppConfig::instance();
    conf.checkDanmuku();

    std::string query = HTTP::encode_form({
        {"api_key", conf.getToken()},
        {"deviceId", conf.getDeviceId()},
    });

    std::string url = fmt::format("{}/socket?{}", "ws" + conf.getUrl().substr(4), query);
    this->ws = std::make_unique<websocket>(url);
}