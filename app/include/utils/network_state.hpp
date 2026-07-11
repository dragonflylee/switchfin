#pragma once

/*
    NetworkState — global "are we browsing offline?" flag (SPEC §4.4).

    Set once at startup when no server is reachable but a local catalog exists
    (main.cpp), and toggled if the user re-connects. Read by the media-source
    routing (media_source.hpp) and by the offline-aware UI.
*/

#include <atomic>

namespace NetworkState {

inline std::atomic_bool g_offline{false};

inline bool isOffline() { return g_offline.load(); }
inline void setOffline(bool v) { g_offline.store(v); }

}  // namespace NetworkState
