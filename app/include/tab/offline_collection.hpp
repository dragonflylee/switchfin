#pragma once

/*
    OfflineCollection — a library grid backed by the local catalog (SPEC §4.4).
    Shows the downloaded top-level items of a section (or of all sections when
    the key is empty, for the offline home) and opens them with localContext so
    fiches render locally and playback uses the local files.
*/

#include <view/recycling_grid.hpp>
#include <string>

class OfflineCollection : public RecyclingGrid {
public:
    /// sectionKey empty = every downloaded movie/show (offline home)
    explicit OfflineCollection(const std::string& sectionKey = "");
};
