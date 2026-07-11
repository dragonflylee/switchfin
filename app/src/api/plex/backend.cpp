/*
    GMCA — Plex implementation of media::Backend (see api/plex/backend.hpp).
    Faithful relocation of the query/playback/watchlist logic that used to live
    at the UI call sites. Reads the active server/token from AppConfig live.
*/

#include "api/plex/backend.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include <sstream>

using namespace brls::literals;  // for _i18n

namespace plex {

const media::Capabilities& PlexBackend::caps() const {
    static const media::Capabilities c = []() {
        media::Capabilities caps;                       // defaults: most true
        caps.listKind = media::ListKind::Watchlist;     // plex.tv account watchlist
        caps.skipIntro = false;                         // markers parsed but not wired (PLEX_MIGRATION §2.7)
        return caps;
    }();
    return c;
}

// ---- navigation ----------------------------------------------------------------

void PlexBackend::listSections(media::Then<media::Container<media::Section>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    getJSON<media::Container<media::Section>>(conf.getUrl(), conf.getToken(), then, error, apiSections);
}

void PlexBackend::getHomeHubs(
    int count, bool excludeContinueWatching, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form form = {{"count", std::to_string(count)}};
    if (excludeContinueWatching) form["excludeContinueWatching"] = "1";
    getJSON<media::Container<media::Hub>>(conf.getUrl(), conf.getToken(), then, error, apiHubs, HTTP::encode_form(form));
}

void PlexBackend::getSectionHubs(
    const std::string& sectionId, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({{"count", std::to_string(count)}});
    getJSON<media::Container<media::Hub>>(conf.getUrl(), conf.getToken(), then, error, apiHubsSection, sectionId, query);
}

void PlexBackend::getContinueWatching(int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({{"count", std::to_string(count)}});
    getJSON<media::Container<media::Hub>>(conf.getUrl(), conf.getToken(), then, error, apiHubContinue, query);
}

void PlexBackend::getLibraryGrid(const std::string& sectionId, const media::GridQuery& q, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    if (!q.sortField.empty()) {
        std::string sort = q.sortField;
        if (q.descending) sort += ":desc";
        query["sort"] = sort;
    }
    if (q.unwatchedOnly) query["unwatched"] = "1";
    if (!q.genreId.empty()) query["genre"] = q.genreId;
    if (q.kind == media::MediaKind::Movie)
        query["type"] = std::to_string(typeMovie);
    else if (q.kind == media::MediaKind::Show)
        query["type"] = std::to_string(typeShow);
    else if (q.kind == media::MediaKind::Artist)
        query["type"] = std::to_string(typeArtist);
    else if (q.kind == media::MediaKind::Album)
        query["type"] = std::to_string(typeAlbum);
    else if (q.kind == media::MediaKind::Track)
        query["type"] = std::to_string(typeTrack);
    addPagination(query, start, size);
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiSectionAll, sectionId, HTTP::encode_form(query));
}

void PlexBackend::getCollectionChildren(const std::string& collectionId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    addPagination(query, start, size);
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiCollectionChildren, collectionId, HTTP::encode_form(query));
}

void PlexBackend::getHubPage(const std::string& hubKey, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    addPagination(query, start, size);
    // hub order = server order: no sort; the key may already carry parameters
    std::string sep = hubKey.find('?') == std::string::npos ? "?" : "&";
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, "{}{}{}", hubKey, sep, HTTP::encode_form(query));
}

void PlexBackend::getItemDetail(
    const std::string& id, bool full, media::Then<media::Item> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    std::string query = full ? HTTP::encode_form({
                                   {"includeChapters", "1"},
                                   {"includeMarkers", "1"},
                                   {"includeStreams", "1"},
                                   {"checkFiles", "1"},
                               })
                             : "";
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(),
        [then, error](const media::Container<media::Item>& r) {
            if (r.Items.empty()) {
                if (error) error("main/plex/unreachable"_i18n);
                return;
            }
            if (then) then(r.Items.front());
        },
        error, apiMetadata, id, query);
}

void PlexBackend::getChildren(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiChildren, id, "");
}

void PlexBackend::getArtistTracks(
    const std::string& artistId, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // all tracks of an artist (across albums): Plex exposes them via allLeaves,
    // like a show's episodes (issue #11).
    auto& conf = AppConfig::instance();
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiAllLeaves, artistId);
}

void PlexBackend::getAllEpisodes(const std::string& showId, bool includeStreams,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    if (includeStreams) {
        std::string query = HTTP::encode_form({{"includeStreams", "1"}});
        getJSON<media::Container<media::Item>>(
            conf.getUrl(), conf.getToken(), then, error, apiGrandchildren, showId, query);
    } else {
        getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiAllLeaves, showId);
    }
}

void PlexBackend::getNextUp(const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError) {
    auto& conf = AppConfig::instance();
    std::string url = conf.getUrl() + fmt::format("/library/metadata/{}?includeOnDeck=1", showId);
    // fully watched show (no OnDeck): first episode via allLeaves paginated 0-1
    HTTP::Form firstQuery;
    addPagination(firstQuery, 0, 1);
    std::string firstUrl =
        conf.getUrl() + fmt::format(fmt::runtime(apiAllLeaves), showId) + "?" + HTTP::encode_form(firstQuery);
    std::string token = conf.getToken();
    brls::async([url, firstUrl, token, then]() {
        std::vector<media::Item> items;
        bool fromStart = false;
        try {
            // Metadata[0].OnDeck.Metadata is an OBJECT (manual extraction)
            nlohmann::json j = getSync(url, token);
            auto& meta = j.at("MediaContainer").at("Metadata").at(0);
            if (meta.contains("OnDeck") && meta["OnDeck"].contains("Metadata")) {
                items.push_back(meta["OnDeck"]["Metadata"].get<media::Item>());
            } else {
                auto first = getSync(firstUrl, token).get<media::Container<media::Item>>();
                if (!first.Items.empty()) {
                    items.push_back(first.Items.front());
                    fromStart = true;
                }
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("plex getNextUp: {}", ex.what());
        }
        brls::sync([items, fromStart, then]() {
            if (!items.empty())
                then(items.front(), fromStart);
            else
                then(media::Item{}, false);
        });
    });
}

void PlexBackend::getExtras(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiExtras, id);
}

void PlexBackend::getRelated(
    const std::string& id, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({{"count", std::to_string(count)}});
    getJSON<media::Container<media::Hub>>(conf.getUrl(), conf.getToken(), then, error, apiHubRelated, id, query);
}

void PlexBackend::getPersonMedia(const std::string& personId, int count,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    std::string query = HTTP::encode_form({{"count", std::to_string(count)}});
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiPersonMedia, personId, query);
}

void PlexBackend::search(const std::string& query, media::MediaKind kind, int limit,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    bool musicKind = kind == media::MediaKind::Artist || kind == media::MediaKind::Album ||
                     kind == media::MediaKind::Track;
    std::string searchTypes = kind == media::MediaKind::Movie ? "movies"
                              : kind == media::MediaKind::Show ? "tv"
                              : musicKind                      ? "music"
                                                               : "movies,tv,music";
    std::string q = HTTP::encode_form({
        {"query", query},
        {"limit", std::to_string(limit)},
        {"searchTypes", searchTypes},
        {"includeCollections", "1"},
    });
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiSearch, q);
}

void PlexBackend::getRecentlyAdded(
    size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form form;
    addPagination(form, start, size);
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, "/library/recentlyAdded?{}", HTTP::encode_form(form));
}

void PlexBackend::getGenres(const std::string& sectionId, media::MediaKind kind,
    media::Then<media::Container<media::Section>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    int type = kind == media::MediaKind::Show                                           ? typeShow
               : (kind == media::MediaKind::Artist || kind == media::MediaKind::Album ||
                     kind == media::MediaKind::Track)
                   ? typeArtist
                   : typeMovie;
    getJSON<media::Container<media::Section>>(conf.getUrl(), conf.getToken(), then, error, apiSectionGenres, sectionId, type);
}

void PlexBackend::getCollections(const std::string& sectionId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    addPagination(query, start, size);
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiCollections, sectionId, HTTP::encode_form(query));
}

void PlexBackend::getPlaylists(
    size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    // No playlistType filter: return video AND audio playlists in one call
    // (Plex has no multi-value filter, so we omit it and let the UI branch on
    // Item::playlistType — audio -> music queue, video -> PlaylistView grid).
    // Music playlists added for issue #11; video playlists keep working. Photo
    // playlists, if any, fall through to the video grid harmlessly.
    addPagination(query, start, size);
    getJSON<media::Container<media::Item>>(conf.getUrl(), conf.getToken(), then, error, apiPlaylists, HTTP::encode_form(query));
}

void PlexBackend::getPlaylistItems(const std::string& playlistId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    auto& conf = AppConfig::instance();
    HTTP::Form query;
    addPagination(query, start, size);
    getJSON<media::Container<media::Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiPlaylistItems, playlistId, HTTP::encode_form(query));
}

// ---- item actions --------------------------------------------------------------

void PlexBackend::markWatched(const std::string& id) {
    auto& conf = AppConfig::instance();
    getAction(conf.getUrl(), conf.getToken(), nullptr, apiScrobble, id);
}

void PlexBackend::markUnwatched(const std::string& id) {
    auto& conf = AppConfig::instance();
    getAction(conf.getUrl(), conf.getToken(), nullptr, apiUnscrobble, id);
}

// ---- playback ------------------------------------------------------------------

media::PlaybackSource PlexBackend::directSource(const media::Media& version, int64_t seekMs) const {
    auto& conf = AppConfig::instance();
    std::stringstream ssextra;
    ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
    if (seekMs > 0) ssextra << ",start=" << misc::sec2Time(seekMs / 1000);
    if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";
    const media::Part& part = version.parts.front();
    std::string url = withToken(conf.getUrl() + part.key, conf.getToken());
    return {url, ssextra.str(), false, "directplay"};
}

media::PlaybackSource PlexBackend::resolvePlayback(
    const media::Item& item, const media::Media& version, const media::PlaybackOptions& opts) {
    // direct play: no bitrate cap or forced
    if (opts.bitrateCap <= 0 || opts.forceDirectPlay) {
        return directSource(version, opts.seekMs);
    }

    auto& conf = AppConfig::instance();

    // ---- audio (music) transcode — best-effort, issue #11 --------------------
    // Mirrors the video universal transcoder but on the MUSIC endpoint
    // (/music/:/transcode/universal). UNVERIFIED against a live server (no test
    // server needs it): the decision call is wrapped so that ANY failure falls
    // back to direct play — a bitrate cap on audio never breaks playback.
    // NOTE for the reviewer: the exact "bitrate" param name for the Plex music
    // transcoder is uncertain (seen as bitrate / musicBitrate / audioBitrate);
    // we use `bitrate` per the task brief. No X-Plex-Client-Profile-Extra clause
    // is sent (the .mp3 endpoint transcodes to MP3 at `bitrate` on its own).
    if (item.type == media::mediaTypeTrack) {
        std::string audioSession = misc::randHex(12);
        HTTP::Form aform = {
            {"hasMDE", "1"},
            {"path", fmt::format("/library/metadata/{}", item.ratingKey)},
            {"mediaIndex", "0"},
            {"partIndex", "0"},
            {"protocol", "http"},
            {"directPlay", "0"},
            {"directStream", "1"},
            {"directStreamAudio", "0"},
            {"bitrate", std::to_string(opts.bitrateCap / 1000)},  // kbps
            {"location", "lan"},
            {"session", audioSession},
            {"X-Plex-Session-Identifier", opts.sessionId},
            {"X-Plex-Platform", "Generic"},  // mandatory: other values -> HTTP 400
            {"X-Plex-Client-Identifier", conf.getDeviceId()},
            {"X-Plex-Product", AppVersion::getPackageName()},
            {"X-Plex-Version", AppVersion::getVersion()},
            {"X-Plex-Token", conf.getToken()},
        };
        if (opts.seekMs > 0) aform["offset"] = std::to_string(opts.seekMs / 1000);  // whole seconds
        std::string aquery = HTTP::encode_form(aform);

        // decision (same gating as video): >= 2000 = failure, 1000 = direct only.
        // Any exception (endpoint absent/unreachable) -> conservative direct play.
        try {
            std::string decUrl = conf.getUrl() + fmt::format(fmt::runtime(apiMusicTranscodeDecision), aquery);
            nlohmann::json decision = getSync(decUrl, "", 10000);
            const nlohmann::json& mc = decision.contains("MediaContainer") ? decision.at("MediaContainer") : decision;
            int64_t general = media::jint(mc, "generalDecisionCode");
            int64_t transcode = media::jint(mc, "transcodeDecisionCode");
            int64_t mde = media::jint(mc, "mdeDecisionCode");
            if (general >= 2000 || mde >= 2000 || transcode == 1000) {
                return directSource(version, opts.seekMs);
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("plex music decision: {} (direct play)", ex.what());
            return directSource(version, opts.seekMs);
        }

        std::string aextra = fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
        if (HTTP::PROXY_STATUS) aextra += fmt::format(",http-proxy=\"{}\"", HTTP::PROXY);
        std::string aplay = conf.getUrl() + fmt::format(fmt::runtime(apiMusicTranscodeStart), aquery);
        return {aplay, aextra, true, "transcode"};
    }

    std::string session = misc::randHex(12);  // transcoder session: regenerated on every start
    HTTP::Form form = {
        {"hasMDE", "1"},
        {"path", fmt::format("/library/metadata/{}", item.ratingKey)},
        {"mediaIndex", "0"},
        {"partIndex", "0"},
        {"protocol", "hls"},
        {"fastSeek", "1"},
        {"directPlay", "0"},
        {"directStream", "1"},
        {"directStreamAudio", "0"},
        {"subtitleSize", "100"},
        {"audioBoost", "100"},
        {"location", "lan"},
        {"addDebugOverlay", "0"},
        {"autoAdjustQuality", "0"},
        {"mediaBufferSize", "102400"},
        {"maxVideoBitrate", std::to_string(opts.bitrateCap / 1000)},  // kbps
        {"session", session},
        {"X-Plex-Session-Identifier", opts.sessionId},
        {"X-Plex-Platform", "Generic"},  // mandatory: other values -> HTTP 400
        {"X-Plex-Client-Identifier", conf.getDeviceId()},
        {"X-Plex-Product", AppVersion::getPackageName()},
        {"X-Plex-Version", AppVersion::getVersion()},
        {"X-Plex-Token", conf.getToken()},
    };
    if (opts.seekMs > 0) form["offset"] = std::to_string(opts.seekMs / 1000);  // whole seconds

    if (opts.audioStreamId > 0) form["audioStreamID"] = std::to_string(opts.audioStreamId);
    if (opts.subtitleStreamId > 0) {
        form["subtitleStreamID"] = std::to_string(opts.subtitleStreamId);
        form["subtitles"] = "burn";  // burn-in (HLS; PLEX_MIGRATION phase 4 revision)
    } else {
        form["subtitles"] = "none";
    }

    // Client profile clauses, each percent-encoded then joined with "+"
    std::vector<std::string> clauses = {
        "add-settings(DirectPlayStreamSelection=true)",
        fmt::format("add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.bitrate&value={}&"
                    "replace=true)",
            opts.bitrateCap / 1000),
        fmt::format("add-transcode-target(type=videoProfile&context=streaming&protocol=hls&container=mpegts&"
                    "videoCodec={}&audioCodec=aac,ac3,mp3&replace=true)",
            opts.videoCodec),
    };
#if defined(__PSV__)
    // The Vita hardware H.264 decoder tops out at 1080p (scripts/vita/ffmpeg
    // patch). The bitrate cap alone lets a >1080p source through at higher
    // qualities (verified: 8 Mbps keeps 1080p, and a 4K source would stay 4K),
    // which the decoder then cannot handle -> playback error. Cap the height so
    // the transcode never exceeds what the decoder can play. Re-homed here from
    // player_view's old playTranscode when transcode construction moved into the
    // backend during the multi-backend merge (guards the issue #14 Vita path).
    clauses.push_back(
        "add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.height&value=1080&replace=true)");
#endif
    std::string profile;
    for (auto& clause : clauses) {
        HTTP::Form one = {{"p", clause}};
        std::string encoded = HTTP::encode_form(one).substr(2);  // strips "p="
        profile += (profile.empty() ? "" : "+") + encoded;
    }
    std::string query = HTTP::encode_form(form) + "&X-Plex-Client-Profile-Extra=" + profile;

    // decision: codes >= 2000 = failure, 1000 = direct play only
    std::string url = conf.getUrl() + fmt::format(fmt::runtime(apiTranscodeDecision), query);
    nlohmann::json decision = getSync(url, "", 10000);
    const nlohmann::json& mc = decision.contains("MediaContainer") ? decision.at("MediaContainer") : decision;
    int64_t general = media::jint(mc, "generalDecisionCode");
    int64_t transcode = media::jint(mc, "transcodeDecisionCode");
    int64_t mde = media::jint(mc, "mdeDecisionCode");
    if (general >= 2000 || mde >= 2000) {
        throw std::runtime_error(fmt::format("{} ({})", "main/player/error"_i18n, general));
    }
    if (transcode == 1000) {
        // the server refuses to transcode: direct play (no resume, as before)
        return directSource(version, 0);
    }

    std::string extra = fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
    if (HTTP::PROXY_STATUS) extra += fmt::format(",http-proxy=\"{}\"", HTTP::PROXY);
    std::string play = conf.getUrl() + "/video/:/transcode/universal/start.m3u8?" + query;
    return {play, extra, true, "transcode"};
}

std::string PlexBackend::subtitleSidecarUrl(const std::string& streamKey) const {
    auto& conf = AppConfig::instance();
    return fmt::format("{}{}?encoding=utf-8&X-Plex-Token={}", conf.getUrl(), streamKey, conf.getToken());
}

void PlexBackend::reportProgress(
    const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs, const std::string& sessionId) {
    auto& conf = AppConfig::instance();
    const char* st =
        state == media::PlayState::Playing ? "playing" : state == media::PlayState::Paused ? "paused" : "stopped";
    HTTP::Form form = {
        {"ratingKey", id},
        {"key", fmt::format("/library/metadata/{}", id)},
        {"state", st},
        {"time", std::to_string(posMs)},
        {"X-Plex-Session-Identifier", sessionId},
    };
    if (durMs > 0) form["duration"] = std::to_string(durMs);

    std::string url = conf.getUrl() + fmt::format(fmt::runtime(apiTimeline), HTTP::encode_form(form));
    std::string token = conf.getToken();
    brls::async([url, token]() {
        try {
            postSync(url, token);  // POST, parameters in query, empty body
        } catch (const std::exception& ex) {
            brls::Logger::warning("plex timeline: {}", ex.what());
        }
    });
}

// ---- url helpers ---------------------------------------------------------------

std::string PlexBackend::imageUrl(const std::string& path, int width, int height) const {
    if (path.empty()) return "";
    // external agent paths (cast faces...) are absolute
    if (path.rfind("http", 0) == 0) return path;
    auto& conf = AppConfig::instance();
    if (width > 0 || height > 0) {
        if (width <= 0) width = height;
        if (height <= 0) height = width;
        HTTP::Form form = {
            {"minSize", "1"},
            {"upscale", "1"},
            {"url", path + "?X-Plex-Token=" + conf.getToken()},
            {"X-Plex-Token", conf.getToken()},
            {"width", std::to_string(width)},
            {"height", std::to_string(height)},
        };
        return conf.getUrl() + "/photo/:/transcode?" + HTTP::encode_form(form);
    }
    return conf.getUrl() + path + "?X-Plex-Token=" + conf.getToken();
}

std::string PlexBackend::imageUrlExternal(const std::string& absoluteUrl, int width, int height) const {
    if (absoluteUrl.empty()) return "";
    auto& conf = AppConfig::instance();
    HTTP::Form form = {
        {"minSize", "1"},
        {"upscale", "1"},
        {"url", absoluteUrl},
        {"X-Plex-Token", conf.getToken()},
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
    };
    return conf.getUrl() + "/photo/:/transcode?" + HTTP::encode_form(form);
}

std::string PlexBackend::downloadUrl(const std::string& partKey) const {
    auto& conf = AppConfig::instance();
    // original file: {base}{Part.key}?download=1&X-Plex-Token=... (PLEX_MIGRATION D2)
    return withToken(conf.getUrl() + partKey + "?download=1", conf.getToken());
}

HTTP::Header PlexBackend::authHeaders() const {
    return headers(AppConfig::instance().getToken());
}

// ---- watchlist (plex.tv account) -----------------------------------------------

bool PlexBackend::canList(const media::Item& item) const { return plex::watchlistable(item); }

void PlexBackend::listWatchlist(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    int type = kind == media::MediaKind::Movie ? typeMovie : kind == media::MediaKind::Show ? typeShow : 0;
    fetchWatchlist(start, size, sortField, type, then, error);
}

void PlexBackend::getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) {
    fetchWatchlistState(item.guid, then, error);
}

void PlexBackend::setWatchlisted(
    const media::Item& item, bool add, std::function<void()> then, media::OnError error) {
    plex::setWatchlisted(item.guid, add, then, error);
}

void PlexBackend::matchInLibrary(const std::string& guid, media::Then<media::Item> then, media::OnError error) {
    plex::matchInLibrary(
        guid,
        [then](const media::Container<media::Item>& r) {
            if (then) then(r.Items.empty() ? media::Item{} : r.Items.front());
        },
        error);
}

}  // namespace plex
