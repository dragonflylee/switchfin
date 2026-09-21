#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

// Values copied from asynchronous property events on the UI thread. The epoch
// also serves as observation userdata, so queued events from a retired media
// session cannot repopulate the next session's cleared cache.
class ObservedProperties {
public:
    using Value = std::variant<int64_t, double, std::string>;

    uint64_t reset() {
        values.clear();
        return ++epoch;
    }

    bool accepts(uint64_t incoming) const { return incoming == epoch; }

    void update(uint64_t incoming, const std::string& name, Value value) {
        if (!accepts(incoming)) return;
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number)) {
            values.erase(name);
            return;
        }
        values.insert_or_assign(name, std::move(value));
    }

    void unavailable(uint64_t incoming, const std::string& name) {
        if (accepts(incoming)) values.erase(name);
    }

    std::string text(const std::string& name) const {
        auto entry = values.find(name);
        if (entry != values.end()) {
            if (const auto* value = std::get_if<std::string>(&entry->second)) return *value;
        }
        return {};
    }

    int64_t integer(const std::string& name, int64_t fallback = 0) const {
        auto entry = values.find(name);
        if (entry != values.end()) {
            if (const auto* value = std::get_if<int64_t>(&entry->second)) return *value;
        }
        return fallback;
    }

    double number(const std::string& name, double fallback = 0.0) const {
        auto entry = values.find(name);
        if (entry != values.end()) {
            if (const auto* value = std::get_if<double>(&entry->second)) return *value;
            if (const auto* value = std::get_if<int64_t>(&entry->second)) return static_cast<double>(*value);
        }
        return fallback;
    }

private:
    // Ordinary playback/control observations use small userdata values.
    uint64_t epoch = uint64_t{1} << 63;
    std::unordered_map<std::string, Value> values;
};
