/*
    GMCA — Jellyfin/Emby implementation of media::Backend (see jellyfin/backend.hpp).
    Translates the Jellyfin API into the neutral media:: model. Reads the active
    server/token/userId from AppConfig live.

    Validated to COMPILE against the documented API; transcode (master.m3u8) and
    download paths are best-effort and need checking against a live server.
*/

#include "api/jellyfin/backend.hpp"
#include "api/jellyfin/types.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include <borealis/core/logger.hpp>
#include <borealis/core/i18n.hpp>
#include <sstream>

using namespace brls::literals;  // hub titles localized like the rest of the UI

namespace jellyfin {

namespace {

std::string userId() { return AppConfig::instance().getUserId(); }
std::string base() { return AppConfig::instance().getUrl(); }
std::string token() { return AppConfig::instance().getToken(); }

/// Plex-style sort token -> Jellyfin SortBy field
std::string mapSort(const std::string& f) {
    if (f == "addedAt") return "DateCreated,SortName";
    if (f == "originallyAvailableAt") return "PremiereDate,SortName";
    if (f == "rating") return "CommunityRating,SortName";
    if (f == "viewCount") return "PlayCount,SortName";
    if (f == "userRating") return "CriticRating,SortName";
    return "SortName";  // titleSort and default
}

std::string kindTypes(media::MediaKind k) {
    if (k == media::MediaKind::Movie) return "Movie";
    if (k == media::MediaKind::Show) return "Series";
    if (k == media::MediaKind::Artist) return "MusicArtist";
    if (k == media::MediaKind::Album) return "MusicAlbum";
    if (k == media::MediaKind::Track) return "Audio";
    return "Movie,Series";
}

/// Async fetch of a paginated container of items.
template <typename T, typename Fn>
void fetchContainer(const std::string& url, Fn parse, media::Then<media::Container<T>> then, media::OnError error) {
    std::string tok = token();
    brls::async([url, tok, parse, then, error]() {
        try {
            auto j = getSync(url, tok);
            auto c = parseContainer<T>(j, parse);
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/// Async fetch of a single item (detail endpoint returns the bare object).
void fetchItem(const std::string& url, media::Then<media::Item> then, media::OnError error) {
    std::string tok = token();
    brls::async([url, tok, then, error]() {
        try {
            auto j = getSync(url, tok);
            media::Item it = parseItem(j);
            brls::sync(std::bind(then, std::move(it)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/// Async fetch of a list of items wrapped as a single titled hub.
void fetchHub(const std::string& url, const std::string& title, const std::string& hubId,
    media::Then<media::Container<media::Hub>> then, media::OnError error) {
    std::string tok = token();
    brls::async([url, tok, title, hubId, then, error]() {
        try {
            auto items = parseContainer<media::Item>(getSync(url, tok), parseItem);
            media::Container<media::Hub> c;
            if (!items.Items.empty()) {
                media::Hub hub;
                hub.title = title;
                hub.hubIdentifier = hubId;
                hub.items = std::move(items.Items);
                c.Items.push_back(std::move(hub));
            }
            c.TotalRecordCount = (long)c.Items.size();
            brls::sync(std::bind(then, std::move(c)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/// Fire-and-forget POST / DELETE (played/favorite/progress).
void postAction(const std::string& url) {
    std::string tok = token();
    brls::async([url, tok]() {
        try {
            HTTP::post(url, "", headers(tok), HTTP::Timeout{});
        } catch (const std::exception& ex) {
            brls::Logger::warning("jellyfin POST {}: {}", url, ex.what());
        }
    });
}

void deleteAction(const std::string& url) {
    std::string tok = token();
    brls::async([url, tok]() {
        try {
            HTTP s;
            HTTP::set_option(s, headers(tok));
            std::ostringstream sink;
            s._delete(url, &sink);
        } catch (const std::exception& ex) {
            brls::Logger::warning("jellyfin DELETE {}: {}", url, ex.what());
        }
    });
}

}  // namespace

const media::Capabilities& JellyfinBackend::caps() const {
    static const media::Capabilities c = []() {
        media::Capabilities caps;
        caps.listKind = media::ListKind::Favorites;  // Jellyfin/Emby favorites
        caps.skipIntro = false;                      // MediaSegments not wired yet
        return caps;
    }();
    return c;
}

// ---- navigation ----------------------------------------------------------------

void JellyfinBackend::listSections(media::Then<media::Container<media::Section>> then, media::OnError error) {
    std::string url = base() + fmt::format(fmt::runtime(apiViews), userId());
    fetchContainer<media::Section>(url, parseSection, then, error);
}

void JellyfinBackend::getHomeHubs(
    int count, bool, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    // The Plex /hubs endpoint returns the whole promoted home (Recently Added
    // per library, …) in one call. Jellyfin/Emby have no equivalent, so we
    // compose the home the way their own web client does: a "Next Up" row, then
    // one "Recently Added" row per movie/show library. (Continue Watching is a
    // separate call, getContinueWatching.) Without this the home was empty on a
    // fresh server — Next Up alone is empty until something has been watched.
    std::string b = base(), tok = token(), uid = userId();
    int cnt = count;
    // titles resolved on the UI thread (i18n map lookup) before going async
    std::string nextupTitle = "main/home/nextup"_i18n;
    std::string latestLabel = "main/home/latest"_i18n;
    brls::async([b, tok, uid, cnt, nextupTitle, latestLabel, then, error]() {
        try {
            media::Container<media::Hub> out;

            // Next Up (next episode of shows already started)
            {
                HTTP::Form f = {{"userId", uid}, {"Limit", std::to_string(cnt)}, {"Fields", itemFieldsValue}};
                auto c = parseContainer<media::Item>(
                    getSync(b + fmt::format(fmt::runtime(apiNextUp), HTTP::encode_form(f)), tok), parseItem);
                if (!c.Items.empty()) {
                    media::Hub h;
                    h.title = nextupTitle;
                    h.hubIdentifier = "home.nextup";  // not "home.ondeck": home_tab skips that one
                    h.items = std::move(c.Items);
                    out.Items.push_back(std::move(h));
                }
            }

            // Recently Added, one row per movie/show library
            auto views = parseContainer<media::Section>(
                getSync(b + fmt::format(fmt::runtime(apiViews), uid), tok), parseSection);
            for (auto& v : views.Items) {
                if (v.type != media::mediaTypeMovie && v.type != media::mediaTypeShow &&
                    v.type != media::mediaTypeArtist)
                    continue;
                HTTP::Form f = {{"ParentId", v.key}, {"Limit", std::to_string(cnt)}, {"Fields", itemFieldsValue}};
                auto c = parseContainer<media::Item>(
                    getSync(b + fmt::format(fmt::runtime(apiLatest), uid, HTTP::encode_form(f)), tok), parseItem);
                if (c.Items.empty()) continue;
                media::Hub h;
                h.title = fmt::format("{} · {}", latestLabel, v.title);
                h.hubIdentifier = "home.latest." + v.key;
                h.items = std::move(c.Items);
                out.Items.push_back(std::move(h));
            }

            out.TotalRecordCount = (long)out.Items.size();
            brls::sync(std::bind(then, std::move(out)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void JellyfinBackend::getSectionHubs(
    const std::string& sectionId, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    HTTP::Form form = {{"ParentId", sectionId}, {"Limit", std::to_string(count)}, {"Fields", itemFieldsValue}};
    std::string url = base() + fmt::format(fmt::runtime(apiLatest), userId(), HTTP::encode_form(form));
    fetchHub(url, "main/home/latest"_i18n, "home.latest", then, error);
}

void JellyfinBackend::getContinueWatching(
    int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    HTTP::Form form = {
        {"Limit", std::to_string(count)},
        {"MediaTypes", "Video"},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiResume), userId(), HTTP::encode_form(form));
    fetchHub(url, "main/home/resume"_i18n, "home.continue", then, error);
}

void JellyfinBackend::getLibraryGrid(const std::string& sectionId, const media::GridQuery& q, size_t start,
    size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // Music artists are virtual entities: /Items?IncludeItemTypes=MusicArtist
    // returns nothing, they must come from the dedicated /Artists endpoint (issue #11).
    if (q.kind == media::MediaKind::Artist) {
        HTTP::Form form = {
            {"ParentId", sectionId},
            {"UserId", userId()},
            {"SortBy", "SortName"},
            {"SortOrder", q.descending ? "Descending" : "Ascending"},
            {"StartIndex", std::to_string(start)},
            {"Limit", std::to_string(size)},
            {"Fields", itemFieldsValue},
        };
        std::string url = base() + "/Artists?" + HTTP::encode_form(form);
        fetchContainer<media::Item>(url, parseItem, then, error);
        return;
    }
    HTTP::Form form = {
        {"ParentId", sectionId},
        {"Recursive", "true"},
        {"IncludeItemTypes", kindTypes(q.kind)},
        {"SortBy", mapSort(q.sortField)},
        {"SortOrder", q.descending ? "Descending" : "Ascending"},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    if (q.unwatchedOnly) form["Filters"] = "IsUnplayed";
    if (!q.genreId.empty()) form["GenreIds"] = q.genreId;
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getArtistTracks(
    const std::string& artistId, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // all tracks of an artist, flattened across albums (issue #11)
    HTTP::Form form = {
        {"ArtistIds", artistId},
        {"IncludeItemTypes", "Audio"},
        {"Recursive", "true"},
        {"UserId", userId()},
        {"SortBy", "Album,ParentIndexNumber,IndexNumber"},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getArtistAlbums(
    const std::string& artistId, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // artist -> albums: Jellyfin artists are virtual, so filter albums by
    // ArtistIds rather than folder ParentId (which returns nothing).
    HTTP::Form form = {
        {"ArtistIds", artistId},
        {"IncludeItemTypes", "MusicAlbum"},
        {"Recursive", "true"},
        {"UserId", userId()},
        {"SortBy", "PremiereDate,ProductionYear,SortName"},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getCollectionChildren(const std::string& collectionId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"ParentId", collectionId},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getHubPage(const std::string& hubKey, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    // Jellyfin hubs carry no "more" key; treat hubKey as a relative path with pagination.
    std::string sep = hubKey.find('?') == std::string::npos ? "?" : "&";
    std::string url =
        base() + hubKey + sep + fmt::format("StartIndex={}&Limit={}&Fields={}", start, size, itemFieldsValue);
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getItemDetail(
    const std::string& id, bool, media::Then<media::Item> then, media::OnError error) {
    std::string url = base() + fmt::format(fmt::runtime(apiItem), userId(), id);
    fetchItem(url, then, error);
}

void JellyfinBackend::getChildren(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    // direct children of any item (a show -> its seasons; a season -> its episodes)
    HTTP::Form form = {{"ParentId", id}, {"UserId", userId()}, {"Fields", itemFieldsValue}};
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getAllEpisodes(const std::string& showId, bool,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {{"userId", userId()}, {"Fields", itemFieldsValue}};
    std::string url = base() + fmt::format(fmt::runtime(apiEpisodes), showId, HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getNextUp(
    const std::string& showId, std::function<void(media::Item, bool)> then, media::OnError) {
    HTTP::Form nextForm = {{"userId", userId()}, {"seriesId", showId}, {"Limit", "1"}, {"Fields", itemFieldsValue}};
    std::string nextUrl = base() + fmt::format(fmt::runtime(apiNextUp), HTTP::encode_form(nextForm));
    HTTP::Form firstForm = {{"userId", userId()}, {"Limit", "1"}, {"Fields", itemFieldsValue}};
    std::string firstUrl = base() + fmt::format(fmt::runtime(apiEpisodes), showId, HTTP::encode_form(firstForm));
    std::string tok = token();
    brls::async([nextUrl, firstUrl, tok, then]() {
        media::Item item;
        bool fromStart = false;
        try {
            auto next = parseContainer<media::Item>(getSync(nextUrl, tok), parseItem);
            if (!next.Items.empty()) {
                item = next.Items.front();
            } else {
                auto first = parseContainer<media::Item>(getSync(firstUrl, tok), parseItem);
                if (!first.Items.empty()) {
                    item = first.Items.front();
                    fromStart = true;
                }
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("jellyfin getNextUp: {}", ex.what());
        }
        brls::sync([item, fromStart, then]() { then(item, fromStart); });
    });
}

void JellyfinBackend::getExtras(
    const std::string& id, media::Then<media::Container<media::Item>> then, media::OnError error) {
    std::string url = base() + fmt::format(fmt::runtime(apiSpecialFeatures), userId(), id);
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getRelated(
    const std::string& id, int count, media::Then<media::Container<media::Hub>> then, media::OnError error) {
    HTTP::Form form = {{"userId", userId()}, {"limit", std::to_string(count)}, {"Fields", itemFieldsValue}};
    std::string url = base() + fmt::format(fmt::runtime(apiSimilar), id, HTTP::encode_form(form));
    fetchHub(url, "main/media/similar"_i18n, "related", then, error);
}

void JellyfinBackend::getPersonMedia(const std::string& personId, int count,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"PersonIds", personId},
        {"Recursive", "true"},
        {"IncludeItemTypes", "Movie,Series"},
        {"Limit", std::to_string(count)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::search(const std::string& query, media::MediaKind kind, int limit,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"searchTerm", query},
        {"Recursive", "true"},
        {"IncludeItemTypes", kindTypes(kind)},
        {"Limit", std::to_string(limit)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getRecentlyAdded(
    size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {{"Limit", std::to_string(size)}, {"Fields", itemFieldsValue}};
    std::string url = base() + fmt::format(fmt::runtime(apiLatest), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getGenres(const std::string& sectionId, media::MediaKind kind,
    media::Then<media::Container<media::Section>> then, media::OnError error) {
    HTTP::Form form = {
        {"parentId", sectionId},
        {"userId", userId()},
        {"IncludeItemTypes", kindTypes(kind)},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiGenres), HTTP::encode_form(form));
    fetchContainer<media::Section>(url, parseSection, then, error);
}

void JellyfinBackend::getCollections(const std::string& sectionId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"ParentId", sectionId},
        {"Recursive", "true"},
        {"IncludeItemTypes", "BoxSet"},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getPlaylists(
    size_t start, size_t size, media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"Recursive", "true"},
        {"IncludeItemTypes", "Playlist"},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getPlaylistItems(const std::string& playlistId, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"userId", userId()},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiPlaylistItems), playlistId, HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

// ---- item actions --------------------------------------------------------------

void JellyfinBackend::markWatched(const std::string& id) {
    postAction(base() + fmt::format(fmt::runtime(apiPlayedItem), userId(), id));
}

void JellyfinBackend::markUnwatched(const std::string& id) {
    deleteAction(base() + fmt::format(fmt::runtime(apiPlayedItem), userId(), id));
}

// ---- playback ------------------------------------------------------------------

media::PlaybackSource JellyfinBackend::resolvePlayback(
    const media::Item& item, const media::Media& version, const media::PlaybackOptions& opts) {
    std::string tok = token();
    auto extra = [&]() {
        std::stringstream e;
        e << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
        if (HTTP::PROXY_STATUS) e << ",http-proxy=\"" << HTTP::PROXY << "\"";
        return e;
    };

    // direct play (static): version.parts[0].key = /{Videos|Audio}/{id}/stream?static=true&...
    // Default for anything with no bitrate cap or when forced (mpv handles
    // mp3/flac/aac natively). Audio with a cap falls through to the audio
    // transcode branch below.
    bool audio = item.type == media::mediaTypeTrack;
    if (opts.bitrateCap <= 0 || opts.forceDirectPlay) {
        std::stringstream e = extra();
        if (opts.seekMs > 0) e << ",start=" << misc::sec2Time(opts.seekMs / 1000);
        std::string url = withToken(base() + version.parts.front().key, tok);
        return {url, e.str(), false, "directplay"};
    }

    // ---- audio (music) transcode — best-effort, issue #11 --------------------
    // Jellyfin Universal Audio endpoint: accepts mp3 direct, else transcodes to
    // aac in an HLS (ts) stream, capped at MaxStreamingBitrate. Placed before
    // the video master path because audio items must use /Audio/, not /Videos/.
    // UNVERIFIED against a live server; direct play (static) stays the default.
    if (audio) {
        std::stringstream e = extra();
        if (opts.seekMs > 0) e << ",start=" << misc::sec2Time(opts.seekMs / 1000);  // mpv-side seek on HLS
        HTTP::Form aform = {
            {"UserId", userId()},
            {"DeviceId", AppConfig::instance().getDeviceId()},
            {"MaxStreamingBitrate", std::to_string(opts.bitrateCap)},  // bps
            {"Container", "mp3"},                                      // client can direct-play mp3
            {"AudioCodec", "aac"},                                     // transcode target codec
            {"TranscodingContainer", "ts"},
            {"TranscodingProtocol", "hls"},
            {"api_key", tok},
        };
        std::string url = base() + fmt::format(fmt::runtime(apiAudioUniversal), item.ratingKey, HTTP::encode_form(aform));
        return {url, e.str(), true, "transcode"};
    }

    // transcode (best-effort HLS master)
    HTTP::Form form = {
        {"mediaSourceId", item.ratingKey},
        {"VideoCodec", opts.videoCodec},
        {"AudioCodec", "aac,mp3"},
        {"VideoBitrate", std::to_string(opts.bitrateCap)},
        {"TranscodingMaxAudioChannels", "2"},
        {"PlaySessionId", opts.sessionId},
        {"api_key", tok},
    };
    if (opts.audioStreamId > 0) form["AudioStreamIndex"] = std::to_string(opts.audioStreamId);
    if (opts.subtitleStreamId > 0) {
        form["SubtitleStreamIndex"] = std::to_string(opts.subtitleStreamId);
        form["SubtitleMethod"] = "Encode";  // burn-in
    }
    if (opts.seekMs > 0) form["StartTimeTicks"] = std::to_string(opts.seekMs * TICKS_PER_MS);
    std::string url = base() + fmt::format(fmt::runtime(apiVideoMaster), item.ratingKey, HTTP::encode_form(form));
    return {url, extra().str(), true, "transcode"};
}

std::string JellyfinBackend::subtitleSidecarUrl(const std::string& streamKey) const {
    if (streamKey.empty()) return "";
    return withToken(base() + streamKey, token());
}

void JellyfinBackend::reportProgress(
    const std::string& id, media::PlayState state, int64_t posMs, int64_t durMs, const std::string& sessionId) {
    nlohmann::json body = {
        {"ItemId", id},
        {"PositionTicks", posMs * TICKS_PER_MS},
        {"PlaySessionId", sessionId},
        {"IsPaused", state == media::PlayState::Paused},
    };
    std::string_view ep = state == media::PlayState::Stopped ? apiSessionsStopped : apiSessionsProgress;
    std::string url = base() + std::string(ep);
    std::string tok = token();
    std::string b = body.dump();
    brls::async([url, tok, b]() {
        try {
            postSync(url, tok, b);
        } catch (const std::exception& ex) {
            brls::Logger::warning("jellyfin progress: {}", ex.what());
        }
    });
}

// ---- url helpers ---------------------------------------------------------------

std::string JellyfinBackend::imageUrl(const std::string& path, int width, int height) const {
    if (path.empty()) return "";
    if (path.rfind("http", 0) == 0) return path;  // external (passed through)
    std::string url = base() + path;
    std::string sep = url.find('?') == std::string::npos ? "?" : "&";
    if (width > 0) {
        url += sep + "fillWidth=" + std::to_string(width);
        sep = "&";
    }
    if (height > 0) {
        url += sep + "fillHeight=" + std::to_string(height);
        sep = "&";
    }
    url += sep + "api_key=" + token();
    return url;
}

std::string JellyfinBackend::downloadUrl(const std::string& partKey) const {
    // partKey = /Videos/{id}/stream?static=true&mediaSourceId=... (original quality)
    return withToken(base() + partKey, token());
}

HTTP::Header JellyfinBackend::authHeaders() const { return headers(token()); }

// ---- personal list: Jellyfin/Emby favorites -----------------------------------

bool JellyfinBackend::canList(const media::Item& item) const {
    return item.type == media::mediaTypeMovie || item.type == media::mediaTypeShow ||
           item.type == media::mediaTypeSeason || item.type == media::mediaTypeEpisode ||
           item.type == media::mediaTypeCollection;
}

void JellyfinBackend::listWatchlist(const std::string& sortField, media::MediaKind kind, size_t start, size_t size,
    media::Then<media::Container<media::Item>> then, media::OnError error) {
    HTTP::Form form = {
        {"Filters", "IsFavorite"},
        {"Recursive", "true"},
        {"IncludeItemTypes", kindTypes(kind)},
        {"SortBy", mapSort(sortField)},
        {"StartIndex", std::to_string(start)},
        {"Limit", std::to_string(size)},
        {"Fields", itemFieldsValue},
    };
    std::string url = base() + fmt::format(fmt::runtime(apiItems), userId(), HTTP::encode_form(form));
    fetchContainer<media::Item>(url, parseItem, then, error);
}

void JellyfinBackend::getWatchlistState(const media::Item& item, media::Then<bool> then, media::OnError error) {
    std::string url = base() + fmt::format(fmt::runtime(apiItem), userId(), item.ratingKey);
    std::string tok = token();
    brls::async([url, tok, then, error]() {
        try {
            nlohmann::json j = getSync(url, tok);
            bool fav = false;
            auto ud = j.find("UserData");
            if (ud != j.end() && ud->is_object()) fav = jbool(*ud, "IsFavorite");
            brls::sync([then, fav]() {
                if (then) then(fav);
            });
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

void JellyfinBackend::setWatchlisted(
    const media::Item& item, bool add, std::function<void()> then, media::OnError) {
    std::string url = base() + fmt::format(fmt::runtime(apiFavoriteItem), userId(), item.ratingKey);
    if (add)
        postAction(url);
    else
        deleteAction(url);
    if (then) brls::sync(then);
}

}  // namespace jellyfin
