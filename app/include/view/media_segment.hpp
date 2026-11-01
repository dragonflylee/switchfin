//
// Media segment (intro / credits / recap ...) helpers.
//
// Pure logic only: no borealis, no network. Keeping it separate makes the
// "what should the player do now" decision unit testable and keeps the
// player views free of tick arithmetic.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace segment {

/// Jellyfin expresses positions in ticks of 100ns
constexpr uint64_t TICKS_PER_SECOND = 10000000;

/// Segments shorter than this are not worth acting on: on a transcoded stream
/// the seek costs more time than the segment itself.
constexpr uint64_t MIN_SEGMENT_TICKS = TICKS_PER_SECOND;

enum class Mode {
    OFF = 0,   ///< ignore segments
    ASK = 1,   ///< show the skip button, wait for the user (default)
    SKIP = 2,  ///< seek to the end of the segment automatically
};

enum class Action {
    NONE,
    ASK,
    SKIP,
    NEXT,  ///< segment without an end (outro): play the next item instead
};

struct Decision {
    Action action = Action::NONE;
    std::string id;
    std::string type;
    double end = 0;
};

inline Mode toMode(int value) {
    switch (value) {
    case 2:
        return Mode::SKIP;
    case 1:
        return Mode::ASK;
    default:
        return Mode::OFF;
    }
}

inline double ticksToSec(uint64_t ticks) { return static_cast<double>(ticks) / TICKS_PER_SECOND; }

inline uint64_t secToTicks(double seconds) { return static_cast<uint64_t>(seconds * TICKS_PER_SECOND); }

/// @brief Whether position @p pos (seconds) is inside the segment
template <typename Seg>
inline bool contains(const Seg& seg, double pos) {
    if (pos < ticksToSec(seg.StartTicks)) return false;
    // no (or a broken) end tick means "until the end of the item"
    if (seg.EndTicks <= seg.StartTicks) return true;
    return pos < ticksToSec(seg.EndTicks);
}

/// @brief Stable identity of a segment, independent of the server side id
template <typename Seg>
inline std::string identity(const Seg& seg) {
    if (!seg.Id.empty()) return seg.Id;
    return seg.Type + "@" + std::to_string(seg.StartTicks);
}

/// @brief First segment containing @p pos (seconds), or nullptr
template <typename Seg>
inline const Seg* find(const std::vector<Seg>& list, double pos) {
    for (const auto& seg : list)
        if (contains(seg, pos)) return &seg;
    return nullptr;
}

/// @brief Decide what to do with the segment under the playhead.
///
/// @param handledId segment already handled, so a prompt or a seek is never repeated
/// @return Decision, the caller marks @c id as handled even when the action is NONE
template <typename Seg>
inline Decision evaluate(const std::vector<Seg>& list, double pos, Mode mode, const std::string& handledId) {
    Decision decision;
    if (mode == Mode::OFF || list.empty()) return decision;

    const Seg* seg = find(list, pos);
    if (seg == nullptr) return decision;

    const std::string key = identity(*seg);
    if (key == handledId) return decision;

    decision.id = key;
    decision.type = seg->Type;
    decision.end = ticksToSec(seg->EndTicks);

    if (seg->EndTicks <= seg->StartTicks) {
        decision.action = Action::NEXT;
    } else if (seg->EndTicks - seg->StartTicks < MIN_SEGMENT_TICKS) {
        decision.action = Action::NONE;
    } else {
        decision.action = mode == Mode::SKIP ? Action::SKIP : Action::ASK;
    }
    return decision;
}

}  // namespace segment
