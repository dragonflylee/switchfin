#include "activity/main_activity.hpp"
#include "utils/config.hpp"
#include "api/http.hpp"
#ifdef PS5_NATIVE_GPU
#include "view/auto_tab_frame.hpp"
#endif

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity: create");

    auto& conf = AppConfig::instance();
    conf.checkDanmuku();

    std::string query = HTTP::encode_form({
#ifdef PS5_NATIVE_GPU
#else
        {"api_key", conf.getToken()},
#endif
        {"deviceId", conf.getDeviceId()},
    });

    std::string url = fmt::format("{}/socket?{}", "ws" + conf.getUrl().substr(4), query);
#ifdef PS5_NATIVE_GPU
    // Reuse the normal API authorization contract. Jellyfin can disable the
    // legacy api_key query parameter while accepting MediaBrowser headers.
    this->ws = std::make_unique<websocket>(url, conf.getAuth(conf.getToken()));
}
#else
    this->ws = std::make_unique<websocket>(url);
}
#endif
