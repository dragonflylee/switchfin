#pragma once

#include <borealis/core/timer.hpp>
#include <borealis/core/singleton.hpp>
#include <nlohmann/json.hpp>
#include <list>
#include <mutex>

namespace analytics {

class Event;

const size_t REPORT_MAX_NUM = 25;

#ifdef NO_GA
#define GA(a) void(a);
#else
#define GA(a, ...) analytics::Analytics::instance().report(a, ##__VA_ARGS__);
#endif /* NO_GA */

class Event {
public:
    std::string name;
    nlohmann::json params;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Event, name, params);

class Analytics : public brls::Singleton<Analytics> {
public:
    Analytics();
    ~Analytics();

    void report(const std::string& event, nlohmann::json params = {});

private:
    void send();

    std::string url;
    std::string client_id;
    std::mutex events_mutex;
    std::list<Event> events;
    brls::RepeatingTimer ticker;
};

}