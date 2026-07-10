#pragma once

/*
    Small offline-mode UI helpers (SPEC §4.4): the empty-state view shown when
    browsing offline with nothing downloaded, and the reconnect action.
*/

namespace brls {
class View;
}

namespace offline_ui {

/// Offline empty state: icon + title + message + a focusable "Retry" button
/// wired to tryReconnect(). Embed it where the offline content would go.
brls::View* makeEmpty();

/// Re-probe the remembered server(s); on success switch back to online mode
/// (recreates MainActivity), otherwise notify that we are still offline.
void tryReconnect();

}  // namespace offline_ui
