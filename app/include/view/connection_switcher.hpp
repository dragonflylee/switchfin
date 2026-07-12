/*
    GMCA — connection switcher grid (Switch-style profile picker).

    One brand-tinted tile per connection (server + profile) + a trailing "+"
    tile. Self-contained Box, used both as the logged-out ServerList content and
    as a detail view over the connected app (sidebar stays visible). rebuild()
    refreshes the grid in place after a connection is deleted.
*/

#pragma once

#include <borealis.hpp>

class ConnectionSwitcher : public brls::Box {
public:
    ConnectionSwitcher();

    /// (Re)builds the tiles + the trailing "+" tile. Does NOT steal focus (used
    /// as tab content: focus must follow the tab mechanism); callers refocus
    /// explicitly after a delete via getDefaultFocus().
    void rebuild();

    /// The active connection's tile (or the first tile) — used when focus enters
    /// the switcher (tab A, activity appear, post-delete refocus).
    brls::View* getDefaultFocus() override;

private:
    brls::Box* tilesBox = nullptr;
    brls::View* firstFocus = nullptr;
};
