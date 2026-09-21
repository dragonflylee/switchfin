#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

namespace ps5_remote_playstate {
struct Request {
    std::string command;
    double seek_seconds = 0;
};

// Decode on the receive thread, inside the websocket's exception boundary.
// Only validated values are captured by the later UI callback.
inline bool decode(const nlohmann::json& data, Request& output) {
    if (!data.is_object()) return false;
    const auto command = data.find("Command");
    if (command == data.end() || !command->is_string()) return false;
    Request next;
    next.command = command->get<std::string>();
    if (next.command == "Seek") {
        const auto position = data.find("SeekPositionTicks");
        if (position == data.end()) return false;
        std::int64_t ticks = 0;
        if (position->is_number_unsigned()) {
            const auto value = position->get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
            ticks = static_cast<std::int64_t>(value);
        } else if (position->is_number_integer()) {
            ticks = position->get<std::int64_t>();
            if (ticks < 0) return false;
        } else {
            return false;
        }
        // Jellyfin ticks are 100 ns. Divide as floating point to retain a
        // fractional playback position; unsigned arithmetic must not wrap it.
        next.seek_seconds = static_cast<double>(ticks) / 10000000.0;
    }
    output = std::move(next);
    return true;
}

// Player provides the existing MPVCore command/event interface. Do not consult
// its asynchronously observed pause cache for idempotent Pause/Unpause commands.
template<class Player>
inline void dispatch(const Request& request, Player& player, const std::string& stop_event) {
    if (request.command == "Pause") {
        player.command("set", "pause", "yes");
    } else if (request.command == "Unpause") {
        player.command("set", "pause", "no");
    } else if (request.command == "PlayPause") {
        player.togglePlay();
    } else if (request.command == "Stop") {
        player.getCustomEvent()->fire(stop_event, nullptr);
    } else if (request.command == "Seek") {
        player.seek(request.seek_seconds, "absolute");
    } else {
        player.getCustomEvent()->fire(request.command, nullptr);
    }
}
} // namespace ps5_remote_playstate
