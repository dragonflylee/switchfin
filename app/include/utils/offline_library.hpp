#pragma once

/*
    OfflineLibrary — local mirror of the Plex library for offline browsing
    (SPEC §4.1). Persists plex::Item snapshots under {config}/downloads/meta/
    and answers the queries the browsing views need (sections/items/children),
    built on the pure logic in offline_catalog.hpp.

    Only real snapshots are persisted; ancestor nodes missing for legacy
    downloads are synthesized in memory at query time (never written to disk).
*/

#include <borealis/core/singleton.hpp>
#include <api/plex/types.hpp>
#include <mutex>
#include <string>
#include <vector>

class OfflineLibrary : public brls::Singleton<OfflineLibrary> {
public:
    /// Load persisted snapshots and back-fill legacy downloads (index.json).
    void init();

    /// Persist (or replace) a node snapshot. Thread-safe.
    void putItem(const plex::Item& item);
    bool hasItem(const std::string& ratingKey) const;
    /// Full "fiche" for a node; false if unknown. Returns the synthesized node
    /// for a legacy ancestor (show/season) that has no persisted snapshot.
    bool getItem(const std::string& ratingKey, plex::Item& out) const;

    /// Libraries present, as plex::Section (mirrors /library/sections).
    std::vector<plex::Section> sections() const;
    /// Top-level items of a section (mirrors /library/sections/{id}/all).
    std::vector<plex::Item> sectionItems(const std::string& sectionKey) const;
    /// Direct children — seasons of a show, episodes of a season
    /// (mirrors /library/metadata/{id}/children).
    std::vector<plex::Item> children(const std::string& ratingKey) const;
    /// All episodes under a show (mirrors /library/metadata/{id}/allLeaves).
    std::vector<plex::Item> leaves(const std::string& showRatingKey) const;

    /// No browsable content — drives the offline-entry decision (main.cpp).
    bool empty() const;

    /// Drop a node, its meta file and its cached artwork.
    void removeItem(const std::string& ratingKey);
    /// Cascade-prune everything no longer backed by a downloaded file: movies
    /// not downloaded, shows/seasons with no downloaded episode and their
    /// orphaned children (SPEC AC18). Uses DownloadManager for the file state.
    void prune();

    std::string metaDir() const;

private:
    void load();           // caller holds the lock
    void rebuild();        // recompute the synthesized tree; caller holds the lock
    void migrateLegacy();  // back-fill nodes from downloads/index.json
    void writeMeta(const plex::Item& item) const;
    std::string metaPath(const std::string& ratingKey) const;

    mutable std::mutex mutex;
    std::vector<plex::Item> nodes;    // raw persisted snapshots
    std::vector<plex::Item> derived;  // nodes + synthesized ancestors
};
