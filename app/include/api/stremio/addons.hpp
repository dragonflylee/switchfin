/*
    GMCA — Stremio addon engine. Owns the loaded set of addons (one per
    configured transportUrl) and answers routing queries: which addons serve a
    given resource/type/id, what catalogs exist, and how to build a resource URL.

    Loading is lazy and thread-safe: the backend verbs run on the brls::async
    pool (multiple threads), and each calls ensureLoaded() inside its async body.
    A std::mutex + `loaded` flag guarantees the manifests are fetched once.
*/

#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "api/stremio/types.hpp"

namespace stremio {

class AddonEngine {
public:
    /// Fetch + parse every configured manifest exactly once. Called from inside
    /// a brls::async body (a backend verb), so it may block on HTTP. A manifest
    /// that fails to load is logged and skipped — never fatal.
    void ensureLoaded();

    /// Force a reload on the next ensureLoaded() (e.g. after the addon list changed).
    void invalidate();

    /// Addons advertising `resource` for the given Stremio `type` (and `id`).
    std::vector<Addon> addonsFor(const std::string& resource, const std::string& type, const std::string& id = "");

    /// Every (addon, catalog) pair across all addons that serve "catalog".
    std::vector<std::pair<Addon, Catalog>> allCatalogs();

    /// Browsable catalogs (skips those requiring an unsupplied extra) of a given
    /// Stremio type ("movie" | "series" | …), in addon/manifest order.
    std::vector<std::pair<Addon, Catalog>> catalogsForType(const std::string& stremioType);

    /// The distinct Stremio content types that have at least one browsable
    /// catalog, in first-seen order (e.g. {"movie","series"}).
    std::vector<std::string> browsableTypes();

    /// Build a resource URL: {base}/{resource}/{type}/{encId}[/{extra}].json.
    /// `extra` entries are joined as k=encodeURIComponent(v) with '&'.
    std::string resourceUrl(const Addon& addon, const std::string& resource, const std::string& type,
        const std::string& id, const std::vector<std::pair<std::string, std::string>>& extra = {}) const;

private:
    std::mutex mtx;
    bool loaded = false;
    std::vector<Addon> addons;
};

}  // namespace stremio
