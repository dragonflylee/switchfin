/*
    pleNx — Jellyfin/Emby protocol: endpoints, auth header, transport helpers,
    and mappers Jellyfin JSON -> neutral media:: model.

    Emby and Jellyfin share the same API shape (Emby is the ancestor). The only
    deltas handled here: the auth header is sent both as `Authorization:
    MediaBrowser ...` (Jellyfin) and `X-Emby-Authorization: ...` (Emby).

    Units: Jellyfin uses TICKS (1 s = 10 000 000 ticks); we convert to ms
    (÷10 000). Container envelope: { Items[], TotalRecordCount, StartIndex }.
    Spec cross-reference: PLEX_MIGRATION.md §1 (the former Jellyfin layer).
*/

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/thread.hpp>
#include "api/http.hpp"
#include "api/media/types.hpp"
#include "utils/config.hpp"

namespace jellyfin {

using media::jbool;
using media::jint;
using media::jnum;
using media::jstr;

constexpr int64_t TICKS_PER_MS = 10000;  // 1 ms = 10 000 ticks

/// ---- Endpoints (formatted at call sites with fmt) --------------------------
const std::string_view apiPublicInfo = "/System/Info/Public";
const std::string_view apiAuthByName = "/Users/AuthenticateByName";
const std::string_view apiQuickConnectInitiate = "/QuickConnect/Initiate";
const std::string_view apiQuickConnectConnect = "/QuickConnect/Connect?secret={}";
const std::string_view apiAuthWithQuickConnect = "/Users/AuthenticateWithQuickConnect";
const std::string_view apiViews = "/Users/{}/Views";
const std::string_view apiResume = "/Users/{}/Items/Resume?{}";
const std::string_view apiNextUp = "/Shows/NextUp?{}";
const std::string_view apiLatest = "/Users/{}/Items/Latest?{}";
const std::string_view apiItems = "/Users/{}/Items?{}";
const std::string_view apiItem = "/Users/{}/Items/{}";
const std::string_view apiSimilar = "/Items/{}/Similar?{}";
const std::string_view apiSeasons = "/Shows/{}/Seasons?{}";
const std::string_view apiEpisodes = "/Shows/{}/Episodes?{}";
const std::string_view apiGenres = "/Genres?{}";
const std::string_view apiPlaybackInfo = "/Items/{}/PlaybackInfo?{}";
const std::string_view apiPlaylistItems = "/Playlists/{}/Items?{}";
const std::string_view apiSessionsPlaying = "/Sessions/Playing";
const std::string_view apiSessionsProgress = "/Sessions/Playing/Progress";
const std::string_view apiSessionsStopped = "/Sessions/Playing/Stopped";
const std::string_view apiPlayedItem = "/Users/{}/PlayedItems/{}";
const std::string_view apiFavoriteItem = "/Users/{}/FavoriteItems/{}";
const std::string_view apiSpecialFeatures = "/Users/{}/Items/{}/SpecialFeatures";
const std::string_view apiVideoStream = "/Videos/{}/stream?static=true&mediaSourceId={}";
const std::string_view apiVideoMaster = "/Videos/{}/master.m3u8?{}";

/// Common `Fields` value requested for rich items (used as a query-param value)
const std::string itemFieldsValue =
    "Overview,Genres,People,ProviderIds,MediaSources,MediaStreams,Chapters,DateCreated,ParentId,SeriesPrimaryImage";

/// ---- Auth header (Emby + Jellyfin) -----------------------------------------
inline std::string authValue(const std::string& token) {
    std::string v = fmt::format("MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
        AppVersion::getPackageName(), AppVersion::getDeviceName(), AppConfig::instance().getDeviceId(),
        AppVersion::getVersion());
    if (!token.empty()) v += fmt::format(", Token=\"{}\"", token);
    return v;
}

inline HTTP::Header headers(const std::string& token) {
    std::string auth = authValue(token);
    return HTTP::Header{
        "Accept: application/json",
        "Content-Type: application/json",
        "Authorization: " + auth,
        "X-Emby-Authorization: " + auth,  // Emby compatibility
    };
}

/// Append the token as a query param (mpv/images/downloads consumed outside HTTP)
inline std::string withToken(const std::string& url, const std::string& token) {
    if (token.empty()) return url;
    return url + (url.find('?') == std::string::npos ? "?" : "&") + "api_key=" + token;
}

/// ---- Type mapping ----------------------------------------------------------
inline std::string mapType(const std::string& t) {
    if (t == "Movie") return media::mediaTypeMovie;
    if (t == "Series") return media::mediaTypeShow;
    if (t == "Season") return media::mediaTypeSeason;
    if (t == "Episode") return media::mediaTypeEpisode;
    if (t == "BoxSet") return media::mediaTypeCollection;
    if (t == "Playlist") return media::mediaTypePlaylist;
    if (t == "Photo") return media::mediaTypePhoto;
    if (t == "Trailer" || t == "Clip") return media::mediaTypeClip;
    return t;
}

/// streamType: Jellyfin MediaStream.Type "Video"/"Audio"/"Subtitle"
inline int mapStreamType(const std::string& t) {
    if (t == "Video") return media::streamTypeVideo;
    if (t == "Audio") return media::streamTypeAudio;
    if (t == "Subtitle") return media::streamTypeSubtitle;
    return 0;
}

/// ---- Mappers (Jellyfin JSON -> media::*) -----------------------------------
inline media::Stream parseStream(const nlohmann::json& j) {
    media::Stream s;
    s.id = jint(j, "Index");
    s.index = jint(j, "Index", -1);
    s.streamType = mapStreamType(jstr(j, "Type"));
    s.codec = jstr(j, "Codec");
    s.language = jstr(j, "Language");
    s.displayTitle = jstr(j, "DisplayTitle");
    s.isDefault = jbool(j, "IsDefault");
    s.forced = jbool(j, "IsForced");
    s.channels = (int)jint(j, "Channels");
    s.selected = jbool(j, "IsDefault");
    // external subtitle: DeliveryUrl (sidecar)
    if (jbool(j, "IsExternal")) s.key = jstr(j, "DeliveryUrl");
    return s;
}

inline media::Media parseMediaSource(const nlohmann::json& j, const std::string& itemId) {
    media::Media m;
    std::string msId = jstr(j, "Id");
    m.container = jstr(j, "Container");
    m.bitrate = jint(j, "Bitrate") / 1000;  // bps -> kbps (parity with Plex Media.bitrate)
    m.duration = jint(j, "RunTimeTicks") / TICKS_PER_MS;
    media::Part p;
    // direct-play / download path (also valid for downloadUrl): the backend
    // tokenizes {base}{key}. mediaSourceId defaults to the item id for single-file items.
    p.key = fmt::format("/Videos/{}/stream?static=true&mediaSourceId={}", itemId, msId.empty() ? itemId : msId);
    p.container = m.container;
    p.duration = m.duration;
    p.accessible = true;
    p.exists = true;
    if (j.contains("MediaStreams") && j["MediaStreams"].is_array()) {
        for (auto& s : j["MediaStreams"]) {
            media::Stream st = parseStream(s);
            if (st.codec == "h264" || st.codec == "hevc" || st.codec == "av1")
                m.videoCodec = st.codec;
            else if (st.streamType == media::streamTypeAudio && m.audioCodec.empty())
                m.audioCodec = st.codec;
            p.streams.push_back(st);
        }
    }
    m.parts.push_back(p);
    return m;
}

inline media::Role parsePerson(const nlohmann::json& j) {
    media::Role r;
    r.id = jstr(j, "Id");
    r.tag = jstr(j, "Name");
    r.role = jstr(j, "Role");
    std::string tag = jstr(j, "PrimaryImageTag");
    if (!tag.empty()) r.thumb = fmt::format("/Items/{}/Images/Primary?tag={}", r.id, tag);
    return r;
}

inline media::Item parseItem(const nlohmann::json& j) {
    media::Item it;
    it.ratingKey = jstr(j, "Id");
    it.key = it.ratingKey;
    it.type = mapType(jstr(j, "Type"));
    it.title = jstr(j, "Name");
    it.summary = jstr(j, "Overview");
    it.year = jint(j, "ProductionYear");
    it.duration = jint(j, "RunTimeTicks") / TICKS_PER_MS;
    it.index = jint(j, "IndexNumber");
    it.parentIndex = jint(j, "ParentIndexNumber");
    it.childCount = jint(j, "ChildCount");
    it.leafCount = jint(j, "RecursiveItemCount", jint(j, "ChildCount"));
    it.parentRatingKey = jstr(j, "SeasonId", jstr(j, "ParentId"));
    it.parentTitle = jstr(j, "SeasonName");
    it.grandparentRatingKey = jstr(j, "SeriesId");
    it.grandparentTitle = jstr(j, "SeriesName");
    it.rating = jnum(j, "CommunityRating");
    it.contentRating = jstr(j, "OfficialRating");
    it.originallyAvailableAt = jstr(j, "PremiereDate");
    it.playlistType = "video";
    // images (path form: /Items/{id}/Images/{kind}?tag=...) — JellyfinBackend::imageUrl finishes the URL
    auto tags = j.find("ImageTags");
    if (tags != j.end() && tags->is_object()) {
        std::string primary = jstr(*tags, "Primary");
        if (!primary.empty()) it.thumb = fmt::format("/Items/{}/Images/Primary?tag={}", it.ratingKey, primary);
        std::string logo = jstr(*tags, "Logo");
        if (!logo.empty()) it.clearLogo = fmt::format("/Items/{}/Images/Logo?tag={}", it.ratingKey, logo);
    }
    if (j.contains("BackdropImageTags") && j["BackdropImageTags"].is_array() && !j["BackdropImageTags"].empty()) {
        it.art = fmt::format("/Items/{}/Images/Backdrop/0?tag={}", it.ratingKey,
            j["BackdropImageTags"][0].get<std::string>());
    }
    // episode/season fallbacks to the show artwork
    if (it.grandparentThumb.empty() && !it.grandparentRatingKey.empty()) {
        std::string st = jstr(j, "SeriesPrimaryImageTag");
        if (!st.empty())
            it.grandparentThumb = fmt::format("/Items/{}/Images/Primary?tag={}", it.grandparentRatingKey, st);
    }
    // user data: watched / resume position
    auto ud = j.find("UserData");
    if (ud != j.end() && ud->is_object()) {
        it.viewCount = jbool(*ud, "Played") ? std::max<int64_t>(1, jint(*ud, "PlayCount")) : jint(*ud, "PlayCount");
        it.viewOffset = jint(*ud, "PlaybackPositionTicks") / TICKS_PER_MS;
        it.viewedLeafCount = it.leafCount - jint(*ud, "UnplayedItemCount", 0);
    }
    // genres (array of plain strings in Jellyfin)
    if (j.contains("Genres") && j["Genres"].is_array())
        for (auto& g : j["Genres"]) it.genres.push_back(g.get<std::string>());
    // people -> roles / directors
    if (j.contains("People") && j["People"].is_array()) {
        for (auto& p : j["People"]) {
            std::string ptype = jstr(p, "Type");
            if (ptype == "Director")
                it.directors.push_back(parsePerson(p));
            else if (ptype == "Actor")
                it.roles.push_back(parsePerson(p));
        }
    }
    // media sources -> versions
    if (j.contains("MediaSources") && j["MediaSources"].is_array())
        for (auto& m : j["MediaSources"]) it.media.push_back(parseMediaSource(m, it.ratingKey));
    // chapters (ticks -> ms; Jellyfin has no end offset)
    if (j.contains("Chapters") && j["Chapters"].is_array()) {
        for (auto& c : j["Chapters"]) {
            media::Chapter ch;
            ch.tag = jstr(c, "Name");
            ch.startTimeOffset = jint(c, "StartPositionTicks") / TICKS_PER_MS;
            it.chapters.push_back(ch);
        }
    }
    // provider ids -> guid (best-effort, for cross-source identity)
    auto pid = j.find("ProviderIds");
    if (pid != j.end() && pid->is_object()) {
        std::string imdb = jstr(*pid, "Imdb");
        if (!imdb.empty()) it.guid = "imdb://" + imdb;
    }
    return it;
}

inline media::Section parseSection(const nlohmann::json& j) {
    media::Section s;
    s.key = jstr(j, "Id");
    s.title = jstr(j, "Name");
    // CollectionType: movies | tvshows | music | ...
    std::string ct = jstr(j, "CollectionType");
    s.type = ct == "movies"  ? "movie"
             : ct == "tvshows" ? "show"
             : ct == "photos"  ? "photo"
                               : ct;
    auto tags = j.find("ImageTags");
    if (tags != j.end() && tags->is_object()) {
        std::string primary = jstr(*tags, "Primary");
        if (!primary.empty()) s.composite = fmt::format("/Items/{}/Images/Primary?tag={}", s.key, primary);
    }
    return s;
}

/// ---- Transport helpers -----------------------------------------------------
inline nlohmann::json getSync(const std::string& url, const std::string& token, long timeout = HTTP::TIMEOUT) {
    std::string resp = HTTP::get(url, headers(token), HTTP::Timeout{timeout});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

inline nlohmann::json postSync(const std::string& url, const std::string& token, const std::string& body = "") {
    std::string resp = HTTP::post(url, body, headers(token), HTTP::Timeout{});
    if (resp.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(resp);
}

/// ---- Container envelope { Items, TotalRecordCount, StartIndex } -------------
template <typename T, typename Fn>
inline media::Container<T> parseContainer(const nlohmann::json& j, Fn parse) {
    media::Container<T> c;
    const nlohmann::json* arr = nullptr;
    if (j.is_array())
        arr = &j;
    else if (j.contains("Items") && j["Items"].is_array())
        arr = &j["Items"];
    if (arr)
        for (auto& e : *arr) c.Items.push_back(parse(e));
    c.TotalRecordCount = (long)jint(j, "TotalRecordCount", c.Items.size());
    c.StartIndex = (long)jint(j, "StartIndex");
    return c;
}

}  // namespace jellyfin
