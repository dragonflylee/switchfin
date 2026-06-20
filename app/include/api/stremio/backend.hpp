/*
    pleNx — Stremio implementation of media::Backend (étape 1: NAVIGATION).
    Translates the Stremio addon protocol (manifests, catalogs, meta, episodes)
    into the neutral media:: model. Stateless, unauthenticated, multi-addon: an
    AddonEngine aggregates the configured addons and routes each request.

    SCOPE: navigation only — catalogs as Sections/Hubs, item detail, seasons,
    episodes, search. Playback (resolvePlayback) is a stub for étape 2; account
    actions (watchlist, profiles, progress) are no-ops for now. See MULTI_BACKEND.md.

    Identity: Item::ratingKey is the OPAQUE "{stremioType}:{stremioId}" codec
    (stremio/types.hpp). Stremio images are ABSOLUTE URLs, passed through verbatim.
*/

#pragma once

#include "api/backend.hpp"
#include "api/stremio/addons.hpp"

namespace stremio {

class StremioBackend : public media::Backend {
public:
    StremioBackend();

    media::BackendType type() const override { return media::BackendType::Stremio; }
    const media::Capabilities& caps() const override { return caps_; }

    // ---- navigation ----------------------------------------------------------
    void listSections(media::Then<media::Container<media::Section>> then, media::OnError error) override;
    std::vector<std::pair<std::string, std::string>> sectionTabs(const std::string& sectionId) override;
    void getHomeHubs(int count, bool excludeContinueWatching, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override;
    void getSectionHubs(const std::string& sectionId, int count, media::Then<media::Container<media::Hub>> then,
        media::OnError error) override;
    void getContinueWatching(int count, media::Then<media::Container<media::Hub>> then, media::OnError error) override;
    void getLibraryGrid(const std::string& sectionId, const media::GridQuery& q, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getCollectionChildren(const std::string& collectionId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getHubPage(const std::string& hubKey, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getItemDetail(const std::string& id, bool full, media::Then<media::Item> then, media::OnError error) override;
    void getChildren(
        const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getAllEpisodes(const std::string& showId, bool includeStreams,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getNextUp(
        const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError error) override;
    void getExtras(const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getRelated(
        const std::string& id, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) override;
    void getPersonMedia(const std::string& personId, int count, media::Then<media::Container<media::Item>> then,
        media::OnError error) override;
    void search(const std::string& query, media::MediaKind kind, int limit,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getRecentlyAdded(
        size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getGenres(const std::string& sectionId, media::MediaKind kind,
        media::Then<media::Container<media::Section>> then, media::OnError error) override;
    void getCollections(const std::string& sectionId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getPlaylists(
        size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getPlaylistItems(const std::string& playlistId, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;

    // ---- item actions --------------------------------------------------------
    void markWatched(const std::string& id) override;
    void markUnwatched(const std::string& id) override;

    // ---- playback (étape 2) --------------------------------------------------
    media::PlaybackSource resolvePlayback(
        const media::Item& item, const media::Media& version, const media::PlaybackOptions& opts) override;
    std::string subtitleSidecarUrl(const std::string& streamKey) const override;
    void reportProgress(const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs,
        const std::string& sessionId) override;

    // ---- url helpers ---------------------------------------------------------
    std::string imageUrl(const std::string& path, int width = 0, int height = 0) const override;
    std::string downloadUrl(const std::string& partKey) const override;
    HTTP::Header authHeaders() const override;

    // ---- personal list: Stremio account library (= watchlist) ----------------
    // Gated by caps().listKind == Watchlist (only when an account is connected).
    bool canList(const media::Item& item) const override;
    void listWatchlist(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
        media::Then<media::Container<media::Item>> then, media::OnError error) override;
    void getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) override;
    void setWatchlisted(const media::Item& item, bool add, std::function<void()> then, media::OnError error) override;

private:
    media::Capabilities caps_;
    AddonEngine engine;
};

}  // namespace stremio
