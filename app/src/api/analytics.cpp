#include "api/analytics.hpp"
#include "api/http.hpp"
#include "utils/config.hpp"
#include "utils/thread.hpp"

namespace analytics {

const std::string GA_ID = "G-SWGSLD5YEC";
const std::string GA_KEY = "ZpMDGqiKR0C2VV_ufgmEiQ";
const std::string GA_URL = "https://www.google-analytics.com/mp/collect";

class Property {
public:
    std::string value;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Property, value);

class Package {
public:
    std::string client_id, user_id;
    int64_t timestamp_micros;
    std::unordered_map<std::string, Property> user_properties;
    std::vector<Event> events;

    Package() {
        user_properties = {
            {"platform", {AppVersion::getPlatform()}},
            {"version", {AppVersion::getVersion()}},
        };
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Package, client_id, user_id, user_properties, events, timestamp_micros);

Analytics::Analytics() {
    this->client_id = AppConfig::instance().getDeviceId();
    this->url = GA_URL + "?" + HTTP::encode_form({{"api_secret", GA_KEY}, {"measurement_id", GA_ID}});

    this->ticker.setCallback([this]() { ThreadPool::instance().submit(&Analytics::send, this); });
    this->ticker.start(10000);
}

Analytics::~Analytics() { this->ticker.stop(); }

void Analytics::report(const std::string& event, nlohmann::json params) {
    // In order for user activity to display in standard reports like Realtime, engagement_time_msec and session_id must be supplied as part of the params for an event.
    // https://developers.google.com/analytics/devguides/collection/protocol/ga4/sending-events?client_type=gtag#optional_parameters_for_reports
    params["engagement_time_msec"] = 100;
    params["session_id"] = this->client_id;

    events_mutex.lock();
    events.push_back(Event{.name = event, .params = params});
    events_mutex.unlock();
}

void Analytics::send() {
    Package pkg;

    this->events_mutex.lock();
    while (this->events.size() > 0) {
        pkg.events.push_back(this->events.front());
        this->events.pop_front();
        if (pkg.events.size() >= REPORT_MAX_NUM) break;
    }
    this->events_mutex.unlock();

    if (pkg.events.empty()) return;

    const auto ts = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::microseconds>(ts);

    pkg.client_id = this->client_id;
    pkg.user_id = AppConfig::instance().getUser().id;
    pkg.timestamp_micros = ms.count();

    try {
        HTTP::post(this->url, nlohmann::json(pkg).dump(), HTTP::Timeout{3000},
            HTTP::Header{"Content-Type: application/json", "Referer: " + AppConfig::instance().getUrl()});
        brls::Logger::debug("report event: {}", pkg.events.size());
    } catch (const std::exception& ex) {
        brls::Logger::warning("report failed: {}", ex.what());
    }
}

}  // namespace analytics