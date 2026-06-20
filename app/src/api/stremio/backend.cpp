/*
    pleNx — Stremio implementation of media::Backend (see stremio/backend.hpp).
    Étape 1: NAVIGATION only. Aggregates the configured addons (AddonEngine) and
    maps the Stremio addon protocol onto the neutral media:: model.

    Async convention mirrors the Jellyfin backend: each verb runs the request on
    brls::async, parses, and calls `then` on the UI thread via brls::sync; on
    failure it calls `error`. ensureLoaded() is invoked INSIDE each async body
    (it may block on the manifest fetches).

    Stubs (later étapes): resolvePlayback (étape 2), markWatched/Unwatched,
    reportProgress, and the empty-Container verbs (collections/playlists/genres/
    related/person/recently-added/extras/continue-watching) which Stremio addons
    do not provide. None of these surface a hard error to the UI.
*/

#include "api/stremio/backend.hpp"
#include "api/stremio/types.hpp"
#include "api/stremio/auth.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/i18n.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

using namespace brls::literals;  // continue-watching hub title localized like the rest of the UI

namespace stremio {

namespace {

/// Map a UI MediaKind to the Stremio resource types it can match. Empty = any.
std::set<std::string> kindToStremioTypes(media::MediaKind k) {
    switch (k) {
        case media::MediaKind::Movie:
            return {"movie"};
        case media::MediaKind::Show:
            return {"series"};
        default:
            return {};  // Any
    }
}

/// Emit an empty Container<T> via `then` without touching the network. Used for
/// the navigation verbs Stremio has no concept of (collections, playlists, …).
template <typename T>
void emptyContainer(media::Then<media::Container<T>> then) {
    if (then) brls::sync([then]() { then(media::Container<T>{}); });
}

// ---- account datastore helpers (library = watchlist + playback state) ----------

/// authKey of the connected account (empty when navigating without one).
std::string accountKey() { return AppConfig::instance().getToken(); }

/// A datastore libraryItem JSON -> media::Item (movie/show row). ratingKey is the
/// opaque "{type}:{_id}"; resume offset/watched come from state.
media::Item itemFromLibrary(const nlohmann::json& j) {
    media::Item it;
    std::string id = jstr(j, "_id");
    std::string type = jstr(j, "type");
    it.ratingKey = type + ":" + id;
    it.key = it.ratingKey;
    it.guid = id;
    it.type = mapType(type);
    it.title = jstr(j, "name");
    it.thumb = jstr(j, "poster");
    auto st = j.find("state");
    if (st != j.end() && st->is_object()) {
        it.viewOffset = jint(*st, "timeOffset");
        it.viewCount = jint(*st, "flaggedWatched") > 0 ? 1 : 0;
        it.duration = jint(*st, "duration");
    }
    return it;
}

/// Find-or-create the libraryItem for `ratingKey` (by its base movie/show id),
/// apply `mutate` to its state, and PUT it back. A new entry is built from /meta
/// (name/poster) and defaults to removed+temp (progress only, not in library).
/// Synchronous — call inside a brls::async body. No-op without an account.
void upsertLibrary(
    AddonEngine& engine, const std::string& ratingKey, const std::function<void(nlohmann::json&)>& mutate) {
    std::string key = accountKey();
    if (key.empty()) return;
    ParsedId pid = parseId(ratingKey);
    std::string baseId = pid.baseId;
    std::string libType = (pid.stremioType == "movie") ? "movie" : "series";

    nlohmann::json items = stremio::datastoreGet(key);
    nlohmann::json found;
    for (auto& it : items)
        if (jstr(it, "_id") == baseId) {
            found = it;
            break;
        }

    std::string now = stremio::nowIso();
    if (found.is_null()) {
        std::string name, poster;
        engine.ensureLoaded();
        for (auto& a : engine.addonsFor("meta", libType, baseId)) {
            try {
                nlohmann::json mj = getSync(engine.resourceUrl(a, "meta", libType, baseId));
                auto m = mj.find("meta");
                if (m != mj.end() && m->is_object()) {
                    name = jstr(*m, "name");
                    poster = jstr(*m, "poster");
                    break;
                }
            } catch (...) {
            }
        }
        found = {
            {"_id", baseId}, {"name", name}, {"type", libType}, {"poster", poster}, {"posterShape", "poster"},
            {"removed", true}, {"temp", true}, {"_ctime", now}, {"_mtime", now},
            {"state", {{"lastWatched", now}, {"timeWatched", 0}, {"timeOffset", 0}, {"overallTimeWatched", 0},
                          {"timesWatched", 0}, {"flaggedWatched", 0}, {"duration", 0}, {"videoId", nullptr},
                          {"watched", nullptr}, {"noNotif", false}}},
            {"behaviorHints", nlohmann::json::object()},
        };
    }
    if (!found.contains("state") || !found["state"].is_object()) found["state"] = nlohmann::json::object();
    mutate(found["state"]);
    found["state"]["lastWatched"] = now;
    found["_mtime"] = now;
    stremio::datastorePut(key, found);
}

// ---- catalog routing + localized labels ----------------------------------------

/// Pre-resolved UI strings (i18n lookups must happen on the UI thread, so a verb
/// loads these before going async and captures them into the worker lambda).
struct L10n {
    std::string movies, series, popular, news, featured;
};
inline L10n loadL10n() {
    return {"main/stremio/movies"_i18n, "main/stremio/series"_i18n, "main/stremio/popular"_i18n,
        "main/stremio/new"_i18n, "main/stremio/featured"_i18n};
}

/// Localized label for a Stremio content type and for a catalog. Cinemeta's
/// canonical catalogs (top/year/imdbRating) get a translated name; any other
/// addon catalog keeps its declared (server) name.
std::string typeLabel(const L10n& l, const std::string& stremioType) {
    if (stremioType == "movie") return l.movies;
    if (stremioType == "series") return l.series;
    return stremioType;
}
std::string catalogLabel(const L10n& l, const Catalog& c) {
    if (c.id == "top") return l.popular;
    if (c.id == "year") return l.news;
    if (c.id == "imdbRating") return l.featured;
    return c.name;
}
/// Like catalogLabel but, when the catalog is unnamed (name fell back to its id),
/// use the addon's name instead (e.g. "publicdomainmovies" -> "Public Domain Movies").
std::string bestCatalogLabel(const L10n& l, const Addon& a, const Catalog& c) {
    std::string lbl = catalogLabel(l, c);
    if (lbl == c.id && !a.manifest.name.empty()) return a.manifest.name;
    return lbl;
}

/// A section/hub key is either a bare Stremio type ("movie"/"series") or a fully
/// routed catalog key "base\ttype\tcatalogId" (built by getSectionHubs/getGenres).
bool isCatalogKey(const std::string& s) { return s.find('\t') != std::string::npos; }
std::string catalogKey(const std::string& base, const std::string& type, const std::string& id) {
    return base + "\t" + type + "\t" + id;
}
bool splitCatalogKey(const std::string& key, std::string& base, std::string& type, std::string& id) {
    auto t1 = key.find('\t');
    if (t1 == std::string::npos) return false;
    auto t2 = key.find('\t', t1 + 1);
    if (t2 == std::string::npos) return false;
    base = key.substr(0, t1);
    type = key.substr(t1 + 1, t2 - t1 - 1);
    id = key.substr(t2 + 1);
    return true;
}

/// Build a catalog resource URL from a base (no Addon object needed).
std::string buildCatalogUrl(const std::string& base, const std::string& type, const std::string& id,
    const std::vector<std::pair<std::string, std::string>>& extra = {}) {
    std::string url = base + "/catalog/" + type + "/" + encodeURIComponent(id);
    if (!extra.empty()) {
        std::string joined;
        for (size_t i = 0; i < extra.size(); ++i) {
            if (i) joined += "&";
            joined += extra[i].first + "=" + encodeURIComponent(extra[i].second);
        }
        url += "/" + joined;
    }
    url += ".json";
    return url;
}

/// Fan out /stream across the addons serving (type,id) and return EVERY source
/// as a neutral media::Media row (parsed quality/codec/size/kind/cache), ordered
/// playable-first by quality (cached debrid before uncached), then the non-
/// playable sources (torrent/external/youtube) by quality. The order is stable
/// so the index the detail page shows matches the one PlayerView re-resolves at
/// play time. Debrid addons resolve infoHash -> a real url server-side, landing
/// here as a playable Direct/Debrid row; infoHash-only/ytId/externalUrl stay
/// non-playable (no local torrent client / browser on console).
std::vector<media::Media> resolveAllStreams(
    AddonEngine& engine, const std::string& stremioType, const std::string& stremioId) {
    std::vector<media::Media> all;
    for (auto& a : engine.addonsFor("stream", stremioType, stremioId)) {
        std::string url = engine.resourceUrl(a, "stream", stremioType, stremioId);
        std::vector<StreamOption> streams;
        try {
            streams = parseStreams(getSync(url, 15000));
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio stream {}: {}", url, ex.what());
            continue;
        }
        for (auto& s : streams) all.push_back(streamToMedia(s, a.manifest.name));
    }
    std::stable_sort(all.begin(), all.end(), [](const media::Media& x, const media::Media& y) {
        if (x.playable() != y.playable()) return x.playable();  // playable first
        int qx = qualityRank(x.videoResolution), qy = qualityRank(y.videoResolution);
        if (qx != qy) return qx > qy;                           // then best quality
        if (x.playable() && x.cached != y.cached) return x.cached;  // then cached debrid first
        return false;
    });
    return all;
}

}  // namespace

StremioBackend::StremioBackend() {
    // Browsable catalogs + composed home rows + ratings, always on. The account-
    // backed features (library = watchlist, watched flag, progress sync, continue
    // watching) are gated on a connected account (authKey persisted as the server
    // access token); without one, the backend is navigation/playback only.
    bool account = !AppConfig::instance().getToken().empty();
    caps_.sections = true;
    caps_.homeHubs = true;
    caps_.continueWatching = account;
    caps_.serverSort = false;
    caps_.serverFilter = false;
    caps_.genres = true;  // genre directories derived from the catalog's genre extra
    caps_.collections = false;
    caps_.playlists = false;
    caps_.related = false;
    caps_.personPages = false;
    caps_.globalSearch = false;
    caps_.recentlyAdded = false;
    caps_.markWatched = account;
    // Stremio's "library" is a server-side list of full items (id/name/poster),
    // displayed and opened like Jellyfin favorites — NOT a provider-guid watchlist
    // (which would route through the Plex-only fetchLibraryGuids/matchInLibrary
    // path in WatchlistTab). So map it onto Favorites.
    caps_.listKind = account ? media::ListKind::Favorites : media::ListKind::None;
    caps_.ratings = true;
    caps_.skipIntro = false;
    caps_.transcode = false;
    caps_.serverProgress = account;
    caps_.downloadOriginal = false;
    caps_.multiProfile = false;
}

// ---- navigation ----------------------------------------------------------------

void StremioBackend::listSections(media::Then<media::Container<media::Section>> then, media::OnError error) {
    // ONE sidebar section per content type (Films, Séries) — not one per catalog
    // (which produced a wall of near-identical icons). The type's catalogs become
    // the section's sub-tabs / rows. Labels localized on the UI thread.
    L10n loc = loadL10n();
    brls::async([this, loc, then, error]() {
        try {
            engine.ensureLoaded();
            media::Container<media::Section> c;
            for (auto& stype : engine.browsableTypes()) {
                if (stype != "movie" && stype != "series") continue;  // channel/tv: niche, skipped
                media::Section s;
                s.key = stype;            // bare type key, resolved to a catalog on demand
                s.type = mapType(stype);  // movie -> media movie, series -> media show
                s.title = typeLabel(loc, stype);
                c.Items.push_back(std::move(s));
            }
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

std::vector<std::pair<std::string, std::string>> StremioBackend::sectionTabs(const std::string& sectionId) {
    // One sub-tab per browsable catalog of the section's type (Populaires,
    // Nouveautés, À la une, Public Domain…). Synchronous read of the already-
    // loaded engine; labels localized on the UI thread.
    L10n loc = loadL10n();
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& pc : engine.catalogsForType(sectionId))
        out.emplace_back(
            catalogKey(pc.first.base, pc.second.type, pc.second.id), bestCatalogLabel(loc, pc.first, pc.second));
    return out;
}

void StremioBackend::getHomeHubs(
    int count, bool, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    int cnt = count;
    L10n loc = loadL10n();
    brls::async([this, cnt, loc, then, error]() {
        try {
            engine.ensureLoaded();
            // Browsable movie then series catalogs, each a row titled with its
            // localized type + catalog name ("Films · Populaires") — no ambiguous
            // untranslated duplicates. Non-browsable catalogs are already excluded.
            std::vector<std::pair<Addon, Catalog>> cats;
            for (auto& p : engine.catalogsForType("movie")) cats.push_back(p);
            for (auto& p : engine.catalogsForType("series")) cats.push_back(p);

            media::Container<media::Hub> out;
            const size_t maxHubs = 8;
            for (auto& pc : cats) {
                if (out.Items.size() >= maxHubs) break;
                const Catalog& cat = pc.second;
                std::string url = buildCatalogUrl(pc.first.base, cat.type, cat.id);
                CatalogResult res;
                try {
                    res = parseCatalog(getSync(url));
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio home catalog {}: {}", url, ex.what());
                    continue;
                }
                if (res.items.empty()) continue;
                media::Hub h;
                h.title = typeLabel(loc, cat.type) + " · " + bestCatalogLabel(loc, pc.first, cat);
                h.hubIdentifier = "home.catalog." + std::to_string(out.Items.size());
                h.key = catalogKey(pc.first.base, cat.type, cat.id);  // "see all" -> getHubPage
                h.more = true;
                if ((int)res.items.size() > cnt) res.items.resize(cnt);
                h.items = std::move(res.items);
                out.Items.push_back(std::move(h));
            }
            out.TotalRecordCount = (long)out.Items.size();
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getSectionHubs(
    const std::string& sectionId, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    // The "Suggestions" sub-tab of a Films/Séries section: one row per browsable
    // catalog of that type (Populaires, Nouveautés, À la une, Public Domain…),
    // each expandable to its full grid.
    std::string stype = sectionId;  // "movie" | "series"
    int cnt = count;
    L10n loc = loadL10n();
    brls::async([this, stype, cnt, loc, then, error]() {
        try {
            engine.ensureLoaded();
            media::Container<media::Hub> out;
            for (auto& pc : engine.catalogsForType(stype)) {
                const Catalog& cat = pc.second;
                std::string url = buildCatalogUrl(pc.first.base, cat.type, cat.id);
                CatalogResult res;
                try {
                    res = parseCatalog(getSync(url));
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio section hub {}: {}", url, ex.what());
                    continue;
                }
                if (res.items.empty()) continue;
                media::Hub h;
                h.title = bestCatalogLabel(loc, pc.first, cat);
                h.hubIdentifier = "section.catalog." + cat.id;
                h.key = catalogKey(pc.first.base, cat.type, cat.id);
                h.more = true;
                if ((int)res.items.size() > cnt) res.items.resize(cnt);
                h.items = std::move(res.items);
                out.Items.push_back(std::move(h));
            }
            out.TotalRecordCount = (long)out.Items.size();
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getContinueWatching(
    int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    std::string key = accountKey();
    if (key.empty()) {
        emptyContainer<media::Hub>(then);
        return;
    }
    int cnt = count;
    std::string title = "main/home/resume"_i18n;  // resolved on the UI thread
    brls::async([key, cnt, title, then, error]() {
        try {
            nlohmann::json items = stremio::datastoreGet(key);
            // in-progress = a resume offset and not yet flagged watched; most
            // recently watched first (ISO-8601 timestamps sort lexicographically).
            std::vector<std::pair<std::string, const nlohmann::json*>> prog;
            for (auto& it : items) {
                auto st = it.find("state");
                if (st == it.end() || !st->is_object()) continue;
                if (jint(*st, "timeOffset") <= 0 || jint(*st, "flaggedWatched") > 0) continue;
                prog.emplace_back(jstr(*st, "lastWatched"), &it);
            }
            std::sort(prog.begin(), prog.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            media::Container<media::Hub> out;
            media::Hub h;
            h.title = title;
            h.hubIdentifier = "home.continue";
            for (size_t i = 0; i < prog.size() && (int)i < cnt; i++) h.items.push_back(itemFromLibrary(*prog[i].second));
            if (!h.items.empty()) out.Items.push_back(std::move(h));
            out.TotalRecordCount = (long)out.Items.size();
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getLibraryGrid(const std::string& sectionId, const media::GridQuery& q, size_t start,
    size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // sectionId is either a bare type ("movie"/"series") -> the type's primary
    // (first) browsable catalog, or a routed catalog key "base\ttype\tcatId"
    // (from a genre drill-down or a "see all" hub).
    std::string sid = sectionId, genreId = q.genreId;
    size_t startCopy = start;
    brls::async([this, sid, genreId, startCopy, then, error]() {
        try {
            engine.ensureLoaded();
            std::string base, ctype, catId;
            if (isCatalogKey(sid)) {
                if (!splitCatalogKey(sid, base, ctype, catId))
                    throw std::runtime_error("stremio: malformed section id");
            } else {
                auto cats = engine.catalogsForType(sid);
                if (cats.empty()) throw std::runtime_error("stremio: no catalog for type");
                base = cats.front().first.base;
                ctype = cats.front().second.type;
                catId = cats.front().second.id;
            }
            std::vector<std::pair<std::string, std::string>> extra;
            if (startCopy > 0) extra.emplace_back("skip", std::to_string(startCopy));
            if (!genreId.empty()) extra.emplace_back("genre", genreId);
            CatalogResult res = parseCatalog(getSync(buildCatalogUrl(base, ctype, catId, extra)));
            media::Container<media::Item> c;
            c.Items = std::move(res.items);
            c.StartIndex = (long)startCopy;
            // Stremio gives no total; report a running count (UI paginates via skip).
            c.TotalRecordCount = (long)(startCopy + c.Items.size());
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getCollectionChildren(
    const std::string&, size_t, size_t, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::getHubPage(
    const std::string& hubKey, size_t start, size_t size, media::Then<media::Container<media::Item>> then,
    media::OnError error) {
    // hubKey = "base\ttype\tcatId" (set on home/section hubs). Page via skip.
    std::string key = hubKey;
    size_t startCopy = start;
    brls::async([this, key, startCopy, then, error]() {
        try {
            std::string base, ctype, catId;
            if (!splitCatalogKey(key, base, ctype, catId)) throw std::runtime_error("stremio: bad hub key");
            std::vector<std::pair<std::string, std::string>> extra;
            if (startCopy > 0) extra.emplace_back("skip", std::to_string(startCopy));
            CatalogResult res = parseCatalog(getSync(buildCatalogUrl(base, ctype, catId, extra)));
            media::Container<media::Item> c;
            c.Items = std::move(res.items);
            c.StartIndex = (long)startCopy;
            c.TotalRecordCount = (long)(startCopy + c.Items.size());
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getItemDetail(
    const std::string& id, bool full, media::Then<media::Item> then, media::OnError error) {
    ParsedId pid = parseId(id);
    // Meta is only addressable on a movie or a whole series; episodes/seasons
    // resolve through the parent series meta (the object carrying videos[]).
    bool isEpisode = (pid.stremioType == "series" && pid.episode >= 0);
    bool playable = (pid.stremioType == "movie") || isEpisode;
    std::string metaType = (pid.stremioType == "movie") ? "movie" : "series";
    std::string metaId = pid.baseId;  // movie/show id without any season:episode suffix
    std::string ratingKey = id;

    brls::async([this, ratingKey, full, isEpisode, playable, metaType, metaId, then, error]() {
        try {
            engine.ensureLoaded();
            auto addons = engine.addonsFor("meta", metaType, metaId);
            if (addons.empty()) throw std::runtime_error("stremio: no meta addon for item");

            // First addon that returns a meta object wins.
            nlohmann::json metaObj;
            bool found = false;
            for (auto& a : addons) {
                std::string url = engine.resourceUrl(a, "meta", metaType, metaId);
                nlohmann::json j;
                try {
                    j = getSync(url);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio meta {}: {}", url, ex.what());
                    continue;
                }
                auto meta = j.find("meta");
                if (meta != j.end() && meta->is_object()) {
                    metaObj = *meta;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::runtime_error("stremio: meta not found");

            media::Item out;
            if (isEpisode) {
                // Pick the matching episode video out of the series meta.
                media::Item show = parseMeta(metaObj);
                for (auto& e : parseEpisodes(metaObj, show))
                    if (e.ratingKey == ratingKey) {
                        out = std::move(e);
                        break;
                    }
                if (out.ratingKey.empty()) {  // not found: minimal episode stub
                    out.ratingKey = ratingKey;
                    out.key = ratingKey;
                    out.type = media::mediaTypeEpisode;
                }
            } else {
                out = parseMeta(metaObj);
            }

            // Resolve ALL sources up front (the detail page shows them as a
            // picker, the player reuses the chosen index). resolvePlayback just
            // returns the chosen source's url. Each Media carries its kind/label/
            // quality; non-playable rows (torrent/external) are kept too so the UI
            // can explain them. Only movies and episodes are directly playable (a
            // show plays via its episodes).
            if (full && playable) {
                ParsedId sp = parseId(ratingKey);
                std::string streamType = (sp.stremioType == "movie") ? "movie" : "series";
                out.media = resolveAllStreams(engine, streamType, sp.stremioId);
            }
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getChildren(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    ParsedId pid = parseId(id);
    std::string stremioType = pid.stremioType;
    std::string stremioId = pid.stremioId;

    // movie: no children.
    if (stremioType == "movie") {
        emptyContainer<media::Item>(then);
        return;
    }

    std::string ratingKeyCopy = id;
    brls::async([this, ratingKeyCopy, stremioType, stremioId, then, error]() {
        try {
            engine.ensureLoaded();
            // Both "series" (show -> seasons) and "season" (-> episodes) resolve
            // through the series meta (the only object carrying videos[]).
            ParsedId pid = parseId(ratingKeyCopy);
            std::string showId = (stremioType == "season") ? pid.baseId : stremioId;

            auto addons = engine.addonsFor("meta", "series", showId);
            if (addons.empty()) throw std::runtime_error("stremio: no meta addon for series");

            nlohmann::json metaObj;
            bool found = false;
            for (auto& a : addons) {
                std::string url = engine.resourceUrl(a, "meta", "series", showId);
                nlohmann::json j;
                try {
                    j = getSync(url);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio meta {}: {}", url, ex.what());
                    continue;
                }
                auto meta = j.find("meta");
                if (meta != j.end() && meta->is_object()) {
                    metaObj = *meta;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::runtime_error("stremio: series meta not found");

            // Build the show stub used to back-reference episodes (grandparent*).
            media::Item show = parseMeta(metaObj);
            // parseMeta sets type=show + ratingKey="series:{id}" from meta.id.

            media::Container<media::Item> c;
            if (stremioType == "series") {
                // Show -> synthesize one Season per distinct video.season.
                std::map<int64_t, int64_t> episodeCounts;  // season -> #episodes
                auto vids = metaObj.find("videos");
                if (vids != metaObj.end() && vids->is_array())
                    for (auto& v : *vids) episodeCounts[jint(v, "season")]++;

                for (auto& kv : episodeCounts) {  // std::map iterates ascending
                    int64_t n = kv.first;
                    media::Item s;
                    s.ratingKey = seasonId(show.guid, n);  // "season:{showId}:{n}"
                    s.key = s.ratingKey;
                    s.type = media::mediaTypeSeason;
                    s.index = n;
                    s.title = (n == 0) ? "Specials" : ("Season " + std::to_string(n));
                    s.leafCount = kv.second;
                    s.thumb = show.thumb;  // seasons reuse the show poster
                    s.art = show.art;
                    s.grandparentRatingKey = show.ratingKey;
                    s.grandparentTitle = show.title;
                    s.parentTitle = show.title;
                    c.Items.push_back(std::move(s));
                }
            } else {  // "season" -> episodes of that season
                int64_t wantSeason = pid.season;
                auto all = parseEpisodes(metaObj, show);
                for (auto& e : all)
                    if (e.parentIndex == wantSeason) c.Items.push_back(std::move(e));
            }
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getAllEpisodes(const std::string& showId, bool,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    ParsedId pid = parseId(showId);
    std::string baseId = (pid.episode >= 0 || pid.season >= 0) ? pid.baseId : pid.stremioId;
    std::string idCopy = showId;
    brls::async([this, idCopy, baseId, then, error]() {
        try {
            engine.ensureLoaded();
            auto addons = engine.addonsFor("meta", "series", baseId);
            if (addons.empty()) throw std::runtime_error("stremio: no meta addon for series");

            nlohmann::json metaObj;
            bool found = false;
            for (auto& a : addons) {
                std::string url = engine.resourceUrl(a, "meta", "series", baseId);
                nlohmann::json j;
                try {
                    j = getSync(url);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio meta {}: {}", url, ex.what());
                    continue;
                }
                auto meta = j.find("meta");
                if (meta != j.end() && meta->is_object()) {
                    metaObj = *meta;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::runtime_error("stremio: series meta not found");

            media::Item show = parseMeta(metaObj);
            media::Container<media::Item> c;
            c.Items = parseEpisodes(metaObj, show);  // already sorted (season, episode)
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getNextUp(
    const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError) {
    ParsedId pid = parseId(showId);
    std::string baseId = (pid.episode >= 0 || pid.season >= 0) ? pid.baseId : pid.stremioId;
    brls::async([this, baseId, then]() {
        media::Item item;
        bool fromStart = false;
        try {
            engine.ensureLoaded();
            auto addons = engine.addonsFor("meta", "series", baseId);
            for (auto& a : addons) {
                std::string url = engine.resourceUrl(a, "meta", "series", baseId);
                nlohmann::json j;
                try {
                    j = getSync(url);
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio getNextUp meta {}: {}", url, ex.what());
                    continue;
                }
                auto meta = j.find("meta");
                if (meta == j.end() || !meta->is_object()) continue;
                media::Item show = parseMeta(*meta);
                auto eps = parseEpisodes(*meta, show);
                if (!eps.empty()) {
                    // No server progress in étape 1: always offer the first episode.
                    item = eps.front();
                    fromStart = true;
                }
                break;
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio getNextUp: {}", ex.what());
        }
        // Contract: never surface a hard error; then(Item{}, false) on no episode.
        brls::sync([item, fromStart, then]() { then(item, fromStart); });
    });
}

void StremioBackend::getExtras(
    const std::string&, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::getRelated(
    const std::string&, int, media::Then<media::Container<media::Hub>> then, media::OnError) {
    emptyContainer<media::Hub>(then);
}

void StremioBackend::getPersonMedia(
    const std::string&, int, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::search(const std::string& query, media::MediaKind kind, int limit,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    std::string q = query;
    int lim = limit;
    std::set<std::string> wantTypes = kindToStremioTypes(kind);
    brls::async([this, q, lim, wantTypes, then, error]() {
        try {
            engine.ensureLoaded();
            auto catalogs = engine.allCatalogs();
            media::Container<media::Item> c;
            std::set<std::string> seen;  // dedup by ratingKey
            for (auto& pc : catalogs) {
                if ((int)c.Items.size() >= lim) break;
                const Addon& addon = pc.first;
                const Catalog& cat = pc.second;
                if (!cat.hasSearch()) continue;
                if (!wantTypes.empty() && wantTypes.count(cat.type) == 0) continue;

                std::string url = engine.resourceUrl(addon, "catalog", cat.type, cat.id, {{"search", q}});
                CatalogResult res;
                try {
                    res = parseCatalog(getSync(url));
                } catch (const std::exception& ex) {
                    brls::Logger::warning("stremio search {}: {}", url, ex.what());
                    continue;
                }
                for (auto& it : res.items) {
                    if ((int)c.Items.size() >= lim) break;
                    if (seen.insert(it.ratingKey).second) c.Items.push_back(std::move(it));
                }
            }
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getRecentlyAdded(
    size_t, size_t, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::getGenres(const std::string& sectionId, media::MediaKind,
    media::Then<media::Container<media::Section>> then, media::OnError error) {
    // Genres declared by the type's primary catalog (Cinemeta exposes a genre
    // extra with options). Each becomes a directory; selecting it drills into the
    // catalog filtered by genre (MediaCollection -> getLibraryGrid with genreId).
    std::string stype = sectionId;  // "movie" | "series"
    // English genre -> localized label, resolved on the UI thread. The Section
    // KEY stays the raw English value (sent to the addon as genre=); only the
    // displayed title is localized. Unknown genres fall back to their raw name.
    std::map<std::string, std::string> gmap = {
        {"Action", "main/stremio/genre/action"_i18n}, {"Adventure", "main/stremio/genre/adventure"_i18n},
        {"Animation", "main/stremio/genre/animation"_i18n}, {"Biography", "main/stremio/genre/biography"_i18n},
        {"Comedy", "main/stremio/genre/comedy"_i18n}, {"Crime", "main/stremio/genre/crime"_i18n},
        {"Documentary", "main/stremio/genre/documentary"_i18n}, {"Drama", "main/stremio/genre/drama"_i18n},
        {"Family", "main/stremio/genre/family"_i18n}, {"Fantasy", "main/stremio/genre/fantasy"_i18n},
        {"History", "main/stremio/genre/history"_i18n}, {"Horror", "main/stremio/genre/horror"_i18n},
        {"Mystery", "main/stremio/genre/mystery"_i18n}, {"Romance", "main/stremio/genre/romance"_i18n},
        {"Sci-Fi", "main/stremio/genre/scifi"_i18n}, {"Sport", "main/stremio/genre/sport"_i18n},
        {"Thriller", "main/stremio/genre/thriller"_i18n}, {"War", "main/stremio/genre/war"_i18n},
        {"Western", "main/stremio/genre/western"_i18n}};
    brls::async([this, stype, gmap, then, error]() {
        try {
            engine.ensureLoaded();
            media::Container<media::Section> c;
            for (auto& pc : engine.catalogsForType(stype)) {
                if (pc.second.genres.empty()) continue;
                for (auto& g : pc.second.genres) {
                    media::Section s;
                    s.key = g;  // genre value passed back as GridQuery.genreId (English)
                    auto it = gmap.find(g);
                    s.title = (it != gmap.end()) ? it->second : g;  // localized display
                    s.type = mapType(stype);
                    c.Items.push_back(std::move(s));
                }
                break;  // the first catalog that declares genres is enough
            }
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getCollections(
    const std::string&, size_t, size_t, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::getPlaylists(
    size_t, size_t, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

void StremioBackend::getPlaylistItems(
    const std::string&, size_t, size_t, media::Then<media::Container<media::Item>> then, media::OnError) {
    emptyContainer<media::Item>(then);
}

// ---- item actions --------------------------------------------------------------

void StremioBackend::markWatched(const std::string& id) {
    if (accountKey().empty()) return;
    std::string rk = id;
    brls::async([this, rk]() {
        try {
            upsertLibrary(engine, rk, [](nlohmann::json& st) {
                st["flaggedWatched"] = 1;
                st["timeOffset"] = 0;  // watched -> clear resume position
            });
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio markWatched: {}", ex.what());
        }
    });
}

void StremioBackend::markUnwatched(const std::string& id) {
    if (accountKey().empty()) return;
    std::string rk = id;
    brls::async([this, rk]() {
        try {
            upsertLibrary(engine, rk, [](nlohmann::json& st) { st["flaggedWatched"] = 0; });
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio markUnwatched: {}", ex.what());
        }
    });
}

// ---- playback (étape 2) --------------------------------------------------------

media::PlaybackSource StremioBackend::resolvePlayback(
    const media::Item&, const media::Media& version, const media::PlaybackOptions&) {
    // getItemDetail already fanned out /stream and stored the chosen playback URL
    // in version.parts[0].key (Stremio has no per-request transcode decision). An
    // empty url means no playable source -> the player shows a "playback failed"
    // dialog. We never throw: a cross-TU throw on the borealis async task loop
    // (which does not wrap tasks in try/catch) would abort the app.
    if (version.parts.empty() || version.parts.front().key.empty()) return {};
    std::string extra = "network-timeout=" + std::to_string(HTTP::TIMEOUT / 100);
    if (HTTP::PROXY_STATUS) extra += ",http-proxy=\"" + HTTP::PROXY + "\"";
    return {version.parts.front().key, extra, false, "directplay"};
}

std::string StremioBackend::subtitleSidecarUrl(const std::string&) const { return ""; }

void StremioBackend::reportProgress(
    const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs, const std::string&) {
    if (accountKey().empty()) return;
    // Persist on pause/stop only — the player calls this every 10 s while playing,
    // far too chatty for a datastoreGet+Put round-trip per tick.
    if (state != media::PlayState::Stopped && state != media::PlayState::Paused) return;
    if (posMs <= 0) return;
    std::string rk = id;
    int64_t pos = posMs, dur = durMs;
    brls::async([this, rk, pos, dur]() {
        try {
            ParsedId pid = parseId(rk);
            std::string videoId = (pid.stremioType == "series" && pid.episode >= 0) ? pid.stremioId : "";
            upsertLibrary(engine, rk, [pos, dur, videoId](nlohmann::json& st) {
                st["timeOffset"] = pos;
                if (dur > 0) st["duration"] = dur;
                if (!videoId.empty()) st["videoId"] = videoId;
            });
        } catch (const std::exception& ex) {
            brls::Logger::warning("stremio reportProgress: {}", ex.what());
        }
    });
}

// ---- url helpers ---------------------------------------------------------------

std::string StremioBackend::imageUrl(const std::string& path, int, int) const {
    // Stremio posters/backdrops/logos are ABSOLUTE URLs — no proxy/resize.
    return path;
}

std::string StremioBackend::downloadUrl(const std::string& partKey) const { return partKey; }

HTTP::Header StremioBackend::authHeaders() const { return HTTP::Header{}; }

// ---- personal list: Stremio account library (= watchlist) ----------------------

bool StremioBackend::canList(const media::Item& item) const {
    if (caps_.listKind == media::ListKind::None) return false;
    // Only whole movies/series live in the library (not seasons/episodes).
    return item.type == media::mediaTypeMovie || item.type == media::mediaTypeShow;
}

void StremioBackend::listWatchlist(const std::string&, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    std::string key = accountKey();
    if (key.empty()) {
        emptyContainer<media::Item>(then);
        return;
    }
    std::set<std::string> wantTypes = kindToStremioTypes(kind);
    size_t s = start, n = size;
    brls::async([key, wantTypes, s, n, then, error]() {
        try {
            nlohmann::json items = stremio::datastoreGet(key);
            std::vector<media::Item> all;
            for (auto& it : items) {
                if (jbool(it, "removed")) continue;  // only items kept in the library
                std::string t = jstr(it, "type");
                if (!wantTypes.empty() && wantTypes.count(t) == 0) continue;
                all.push_back(itemFromLibrary(it));
            }
            media::Container<media::Item> c;
            for (size_t i = s; i < all.size() && i < s + n; i++) c.Items.push_back(std::move(all[i]));
            c.StartIndex = (long)s;
            c.TotalRecordCount = (long)all.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) {
    std::string key = accountKey();
    if (key.empty()) {
        if (then) then(false);
        return;
    }
    std::string baseId = parseId(item.ratingKey).baseId;
    brls::async([key, baseId, then, error]() {
        try {
            nlohmann::json items = stremio::datastoreGet(key);
            bool in = false;
            for (auto& it : items)
                if (jstr(it, "_id") == baseId && !jbool(it, "removed")) {
                    in = true;
                    break;
                }
            brls::sync([then, in]() {
                if (then) then(in);
            });
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void StremioBackend::setWatchlisted(
    const media::Item& item, bool add, std::function<void()> then, media::OnError error) {
    std::string key = accountKey();
    if (key.empty()) {
        if (error) error("Compte Stremio requis");
        return;
    }
    ParsedId pid = parseId(item.ratingKey);
    std::string baseId = pid.baseId;
    std::string libType = (pid.stremioType == "movie") ? "movie" : "series";
    std::string title = item.title, poster = item.thumb;
    int64_t dur = item.duration;
    bool addCopy = add;
    brls::async([key, baseId, libType, title, poster, dur, addCopy, then, error]() {
        try {
            // Reuse an existing entry (preserve its playback state); else build one.
            nlohmann::json items = stremio::datastoreGet(key);
            nlohmann::json found;
            for (auto& it : items)
                if (jstr(it, "_id") == baseId) {
                    found = it;
                    break;
                }
            std::string now = stremio::nowIso();
            if (found.is_null()) {
                found = {
                    {"_id", baseId}, {"name", title}, {"type", libType}, {"poster", poster},
                    {"posterShape", "poster"}, {"removed", !addCopy}, {"temp", false}, {"_ctime", now},
                    {"_mtime", now},
                    {"state", {{"lastWatched", nullptr}, {"timeWatched", 0}, {"timeOffset", 0},
                                  {"overallTimeWatched", 0}, {"timesWatched", 0}, {"flaggedWatched", 0},
                                  {"duration", dur}, {"videoId", nullptr}, {"watched", nullptr},
                                  {"noNotif", false}}},
                    {"behaviorHints", nlohmann::json::object()},
                };
            } else {
                found["removed"] = !addCopy;
                found["temp"] = false;  // explicit library membership, not a transient watch
                if (!title.empty()) found["name"] = title;
                if (!poster.empty()) found["poster"] = poster;
                found["_mtime"] = now;
            }
            stremio::datastorePut(key, found);
            if (then) brls::sync(then);
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

}  // namespace stremio
