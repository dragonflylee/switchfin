/*
    GMCA — first screen of the add-server flow: pick the backend type.
    A neutral, focusable list (Plex / Jellyfin / Emby / Stremio) rendered
    in-content as a view of the ServerList AppletFrame (pushed via
    View::present), so it keeps the footer. Each cell present()s its dedicated
    sign-in screen (PlexAdd, JellyfinAdd or StremioAdd) onto the same content
    stack; B returns here.
*/

#pragma once

#include <borealis.hpp>

class ServerTypeChoose : public brls::Box {
public:
    ServerTypeChoose();
    ~ServerTypeChoose() override;

    /// Focus the first cell on entry (deterministic, no phantom focus).
    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(brls::DetailCell, cellPlex, "type/plex");
    BRLS_BIND(brls::DetailCell, cellJellyfin, "type/jellyfin");
    BRLS_BIND(brls::DetailCell, cellEmby, "type/emby");
    BRLS_BIND(brls::DetailCell, cellStremio, "type/stremio");
};
