/*
    pleNx — native models for the Plex API.
    Verified specification: PLEX_MIGRATION.md §2.

    Units: durations/positions in MILLISECONDS, timestamps in epoch SECONDS,
    transcoder seek offset in whole SECONDS (cf. §2.6).
*/

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace plex {

/// ---- plex.tv (authentication & discovery) ----------------------------------
const std::string tvApiBase = "https://clients.plex.tv/api/v2";
const std::string tvApiFallback = "https://plex.tv/api/v2";
// "Weak" PIN (4 characters): required for manual entry on
// https://plex.tv/link. `?strong=true` generates a long code meant to be
// passed as a URL parameter to app.plex.tv/auth, not to be typed.
const std::string_view tvApiPins = "{}/pins";
const std::string_view tvApiPinPoll = "{}/pins/{}";
const std::string_view tvApiUser = "{}/user";
const std::string_view tvApiResources = "{}/resources?includeHttps=1&includeRelay=1&includeIPv6=1";
const std::string_view tvApiHomeUsers = "{}/home/users";
const std::string_view tvApiSwitchUser = "{}/home/users/{}/switch?{}";
// URL to show to the user (phone/browser)
const std::string tvLinkUrl = "https://plex.tv/link";

/// ---- plex.tv provider (ACCOUNT watchlist) -----------------------------------
/// The watchlist belongs to the plex.tv account, not the server: these hosts
/// must be queried with the ACCOUNT token (AppConfig::getAccountToken()),
/// never the server token. Endpoints come from the official provider API
/// (Plex Web/python-plexapi), ALL verified with real GETs on 2026-06-10
/// against a live account (JSON responses observed).
const std::string discoverApiBase = "https://discover.provider.plex.tv";
const std::string metadataApiBase = "https://metadata.provider.plex.tv";
/// Paginated list (X-Plex-Container-*); `sort=watchlistedAt:desc` verified
/// (asc/desc do invert the order). Metadata[]: PROVIDER ratingKey,
/// guid plex://movie|show/..., thumb/art as ABSOLUTE URLs (tmdb,
/// metadata-static.plex.tv — original sizes, must be proxied).
const std::string_view apiWatchlistAll = "/library/sections/watchlist/all?{}";
/// PUT, ratingKey = PROVIDER ratingKey (last segment of the plex:// guid)
const std::string_view apiWatchlistAdd = "/actions/addToWatchlist?ratingKey={}";
const std::string_view apiWatchlistRemove = "/actions/removeFromWatchlist?ratingKey={}";
/// Watchlist state of a title: the SERVER metadata exposes no field
/// (verified); on metadata.provider with includeUserState=1, the
/// `watchlistedAt` field (epoch s) is present iff the title is watchlisted
/// (verified on a watchlisted and a non-watchlisted movie).
const std::string_view apiProviderMetadata = "/library/metadata/{}?includeUserState=1";
/// Provider -> server mapping: server items bearing this guid
/// (verified: GET /library/all?guid=plex%3A%2F%2Fmovie%2F... -> Metadata[1]).
const std::string_view apiLibraryMatch = "/library/all?{}";

/// ---- Media server (PMS) -----------------------------------------------------
const std::string_view apiIdentity = "/identity";
const std::string_view apiSections = "/library/sections";
const std::string_view apiSectionAll = "/library/sections/{}/all?{}";
const std::string_view apiSectionGenres = "/library/sections/{}/genre?type={}";
const std::string_view apiSectionSorts = "/library/sections/{}/sorts";
const std::string_view apiSectionRecent = "/library/sections/{}/recentlyAdded?{}";
const std::string_view apiHubs = "/hubs?{}";
const std::string_view apiHubsSection = "/hubs/sections/{}?{}";
const std::string_view apiHubContinue = "/hubs/continueWatching?{}";
const std::string_view apiHubRelated = "/hubs/metadata/{}/related?{}";
const std::string_view apiMetadata = "/library/metadata/{}?{}";
const std::string_view apiChildren = "/library/metadata/{}/children?{}";
/// filmography of a person
const std::string_view apiPersonMedia = "/library/people/{}/media?{}";
const std::string_view apiGrandchildren = "/library/metadata/{}/grandchildren?{}";
/// all episodes of a show, across all seasons (Container<Item>)
const std::string_view apiAllLeaves = "/library/metadata/{}/allLeaves";
const std::string_view apiExtras = "/library/metadata/{}/extras";
const std::string_view apiSearch = "/library/search?{}";
const std::string_view apiCollections = "/library/sections/{}/collections?{}";
const std::string_view apiCollectionChildren = "/library/collections/{}/children?{}";
/// playlists (verified on a real server 2026-06-10: Metadata[] type "playlist",
/// composite/leafCount/duration/smart; X-Plex-Container-* pagination supported)
const std::string_view apiPlaylists = "/playlists?{}";
const std::string_view apiPlaylistItems = "/playlists/{}/items?{}";
const std::string_view apiScrobble = "/:/scrobble?key={}&identifier=com.plexapp.plugins.library";
const std::string_view apiUnscrobble = "/:/unscrobble?key={}&identifier=com.plexapp.plugins.library";
const std::string_view apiTimeline = "/:/timeline?{}";
const std::string_view apiPhotoTranscode = "/photo/:/transcode?{}";
const std::string_view apiTranscodeDecision = "/video/:/transcode/universal/decision?{}";
const std::string_view apiTranscodeStart = "/video/:/transcode/universal/start?{}";
/// Tears a transcode session down server-side (frees the transcoder). Must be
/// called on every (re)start and on player exit or sessions pile up orphaned.
const std::string_view apiTranscodeStop = "/video/:/transcode/universal/stop?{}";
const std::string_view apiPartIndexes = "/library/parts/{}/indexes/sd";

/// Numeric types (`type=` parameter)
const int typeMovie = 1;
const int typeShow = 2;
const int typeSeason = 3;
const int typeEpisode = 4;

/// Text types (`type` field of Metadata)
const std::string mediaTypeMovie = "movie";
const std::string mediaTypeShow = "show";
const std::string mediaTypeSeason = "season";
const std::string mediaTypeEpisode = "episode";
const std::string mediaTypeClip = "clip";
const std::string mediaTypeCollection = "collection";
const std::string mediaTypePhoto = "photo";
const std::string mediaTypePlaylist = "playlist";

/// streamType values
const int streamTypeVideo = 1;
const int streamTypeAudio = 2;
const int streamTypeSubtitle = 3;

/// ---- Lenient JSON helpers ---------------------------------------------------
/// Plex omits absent fields and sometimes returns numbers as strings;
/// these helpers absorb both cases without throwing.
inline std::string jstr(const nlohmann::json& j, const char* key, const std::string& def = "") {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
    return def;
}

inline int64_t jint(const nlohmann::json& j, const char* key, int64_t def = 0) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_number()) return it->get<int64_t>();
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

inline double jnum(const nlohmann::json& j, const char* key, double def = 0.0) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

inline bool jbool(const nlohmann::json& j, const char* key, bool def = false) {
    auto it = j.find(key);
    if (it == j.end()) return def;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number()) return it->get<int64_t>() != 0;
    if (it->is_string()) return it->get<std::string>() == "1" || it->get<std::string>() == "true";
    return def;
}

/// Arrays of `[{"tag": "..."}]` objects (Genre, Director, Writer...)
inline std::vector<std::string> jtags(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (auto& e : *it) out.push_back(jstr(e, "tag"));
    return out;
}

/// ---- Authentication (plex.tv) -------------------------------------------------

/// POST /api/v2/pins
struct PinResult {
    int64_t id = 0;
    std::string code;
    std::string authToken;  // filled once the user has validated on plex.tv/link
};
inline void from_json(const nlohmann::json& j, PinResult& r) {
    r.id = jint(j, "id");
    r.code = jstr(j, "code");
    r.authToken = jstr(j, "authToken");
}

/// GET /api/v2/user
struct AccountUser {
    std::string uuid;
    std::string username;
    std::string email;
    std::string thumb;
};
inline void from_json(const nlohmann::json& j, AccountUser& r) {
    r.uuid = jstr(j, "uuid");
    r.username = jstr(j, "username");
    r.email = jstr(j, "email");
    r.thumb = jstr(j, "thumb");
}

/// GET /api/v2/home/users
struct HomeUser {
    std::string uuid;
    std::string title;
    std::string thumb;
    bool isProtected = false;  // `protected`: PIN required for /switch
    bool admin = false;
};
inline void from_json(const nlohmann::json& j, HomeUser& r) {
    r.uuid = jstr(j, "uuid");
    r.title = jstr(j, "title");
    r.thumb = jstr(j, "thumb");
    r.isProtected = jbool(j, "protected");
    r.admin = jbool(j, "admin");
}

/// A candidate connection to a server (resources -> connections[])
struct Connection {
    std::string protocol;  // "http" | "https"
    std::string address;
    int port = 32400;
    std::string uri;  // e.g. https://192-168-1-100.<hash>.plex.direct:32400
    bool local = false;
    bool relay = false;
};
inline void from_json(const nlohmann::json& j, Connection& r) {
    r.protocol = jstr(j, "protocol");
    r.address = jstr(j, "address");
    r.port = (int)jint(j, "port", 32400);
    r.uri = jstr(j, "uri");
    r.local = jbool(j, "local");
    r.relay = jbool(j, "relay");
}

/// GET /api/v2/resources, filtered on provides=server
struct ServerResource {
    std::string name;
    std::string clientIdentifier;  // machine id of the server
    std::string accessToken;       // token SPECIFIC to this server (!= account token)
    bool owned = true;
    std::vector<Connection> connections;
};
inline void from_json(const nlohmann::json& j, ServerResource& r) {
    r.name = jstr(j, "name");
    r.clientIdentifier = jstr(j, "clientIdentifier");
    r.accessToken = jstr(j, "accessToken");
    r.owned = jbool(j, "owned", true);
    if (j.contains("connections") && j["connections"].is_array())
        r.connections = j["connections"].get<std::vector<Connection>>();
}

/// ---- Media models ---------------------------------------------------------------

/// Library (Directory from /library/sections)
struct Section {
    std::string key;   // numeric id of the section (as a string)
    std::string type;  // movie | show | artist | photo
    std::string title;
    std::string uuid;
    std::string composite;  // poster mosaic generated by the server
    std::string art;
    bool hidden = false;
};
inline void from_json(const nlohmann::json& j, Section& r) {
    r.key = jstr(j, "key");
    r.type = jstr(j, "type");
    r.title = jstr(j, "title");
    r.uuid = jstr(j, "uuid");
    r.composite = jstr(j, "composite");
    r.art = jstr(j, "art");
    r.hidden = jbool(j, "hidden");
}

struct Stream {
    int64_t id = 0;
    int streamType = 0;  // 1 video, 2 audio, 3 subtitle
    int64_t index = -1;
    std::string codec;
    std::string language;
    std::string languageTag;
    std::string displayTitle;
    bool selected = false;
    bool isDefault = false;
    bool forced = false;
    int channels = 0;
    std::string key;  // external subtitle: sidecar path (/library/streams/{id})
};
inline void from_json(const nlohmann::json& j, Stream& r) {
    r.id = jint(j, "id");
    r.streamType = (int)jint(j, "streamType");
    r.index = jint(j, "index", -1);
    r.codec = jstr(j, "codec");
    r.language = jstr(j, "language");
    r.languageTag = jstr(j, "languageTag");
    r.displayTitle = jstr(j, "displayTitle", jstr(j, "extendedDisplayTitle"));
    r.selected = jbool(j, "selected");
    r.isDefault = jbool(j, "default");
    r.forced = jbool(j, "forced");
    r.channels = (int)jint(j, "channels");
    r.key = jstr(j, "key");
}

struct Part {
    int64_t id = 0;
    std::string key;  // direct play path: {base}{key}?X-Plex-Token=...
    std::string container;
    int64_t size = 0;
    int64_t duration = 0;  // ms
    bool accessible = true;
    bool exists = true;
    std::vector<Stream> streams;
};
inline void from_json(const nlohmann::json& j, Part& r) {
    r.id = jint(j, "id");
    r.key = jstr(j, "key");
    r.container = jstr(j, "container");
    r.size = jint(j, "size");
    r.duration = jint(j, "duration");
    r.accessible = jbool(j, "accessible", true);
    r.exists = jbool(j, "exists", true);
    if (j.contains("Stream") && j["Stream"].is_array()) r.streams = j["Stream"].get<std::vector<Stream>>();
}

struct Media {
    int64_t id = 0;
    std::string videoResolution;  // "1080", "4k"...
    std::string videoCodec;
    std::string audioCodec;
    std::string container;
    int64_t bitrate = 0;  // kbps
    int width = 0;
    int height = 0;
    int64_t duration = 0;  // ms
    std::vector<Part> parts;
};
inline void from_json(const nlohmann::json& j, Media& r) {
    r.id = jint(j, "id");
    r.videoResolution = jstr(j, "videoResolution");
    r.videoCodec = jstr(j, "videoCodec");
    r.audioCodec = jstr(j, "audioCodec");
    r.container = jstr(j, "container");
    r.bitrate = jint(j, "bitrate");
    r.width = (int)jint(j, "width");
    r.height = (int)jint(j, "height");
    r.duration = jint(j, "duration");
    if (j.contains("Part") && j["Part"].is_array()) r.parts = j["Part"].get<std::vector<Part>>();
}

struct Chapter {
    std::string tag;            // chapter title
    int64_t startTimeOffset = 0;  // ms
    int64_t endTimeOffset = 0;    // ms
};
inline void from_json(const nlohmann::json& j, Chapter& r) {
    r.tag = jstr(j, "tag");
    r.startTimeOffset = jint(j, "startTimeOffset");
    r.endTimeOffset = jint(j, "endTimeOffset");
}

/// Intro/credits marker (type = "intro" | "credits"; ms)
struct Marker {
    std::string type;
    int64_t startTimeOffset = 0;
    int64_t endTimeOffset = 0;
};
inline void from_json(const nlohmann::json& j, Marker& r) {
    r.type = jstr(j, "type");
    r.startTimeOffset = jint(j, "startTimeOffset");
    r.endTimeOffset = jint(j, "endTimeOffset");
}

struct Role {
    std::string id;    // numeric identifier (person page, filmography)
    std::string tag;   // person name
    std::string role;  // character
    std::string thumb;
};
inline void from_json(const nlohmann::json& j, Role& r) {
    if (j.contains("id")) {
        if (j.at("id").is_number())
            r.id = std::to_string(j.at("id").get<int64_t>());
        else
            r.id = jstr(j, "id");
    }
    r.tag = jstr(j, "tag");
    r.role = jstr(j, "role");
    r.thumb = jstr(j, "thumb");
}

/// Library item (`Metadata` object)
struct Item {
    std::string ratingKey;  // identifier, equivalent of Jellyfin Item.Id
    std::string key;        // detail path (/library/metadata/{ratingKey})
    std::string guid;
    std::string type;  // movie | show | season | episode | clip | collection
    std::string title;
    std::string summary;
    int64_t year = 0;
    std::string thumb;      // relative poster path
    std::string art;        // relative backdrop path
    std::string clearLogo;  // cut-out logo (Image[] array, type clearLogo)
    int64_t duration = 0;    // ms
    int64_t viewOffset = 0;  // ms — resume position
    int64_t viewCount = 0;   // > 0 means watched
    int64_t addedAt = 0;     // epoch s
    int64_t lastViewedAt = 0;
    int64_t watchlistedAt = 0;  // epoch s — metadata.provider + includeUserState=1 (0 = absent)
    int64_t index = 0;        // episode number (or season number on a season)
    int64_t parentIndex = 0;  // season number (on an episode)
    int64_t leafCount = 0;    // episode count (show/season)
    int64_t viewedLeafCount = 0;
    int64_t childCount = 0;
    // playlists (type "playlist"; GET /playlists?playlistType=video)
    std::string composite;     // 1:1 mosaic generated by the server
    bool smart = false;        // smart playlist (dynamic filter)
    std::string playlistType;  // video | audio | photo — only video ones are playable here
    std::string contentRating;
    double rating = 0.0;          // critic rating 0-10
    double audienceRating = 0.0;  // audience rating 0-10
    std::string ratingImage;          // critic source: imdb://…, rottentomatoes://image.rating.ripe|rotten, themoviedb://…
    std::string audienceRatingImage;  // audience source: rottentomatoes://image.rating.upright|spilled
    std::string originallyAvailableAt;  // YYYY-MM-DD
    std::string parentRatingKey;        // season (from an episode)
    std::string parentTitle;
    std::string parentThumb;
    std::string grandparentRatingKey;  // show (from an episode)
    std::string grandparentTitle;
    std::string grandparentThumb;
    std::string grandparentArt;
    std::string librarySectionID;     // numeric id of the owning library (as string)
    std::string librarySectionTitle;  // display name of the owning library
    std::vector<std::string> genres;
    std::vector<Role> roles;
    std::vector<Role> directors;  // Director: same shape as Role (id/tag/thumb)
    std::vector<Media> media;
    std::vector<Chapter> chapters;
    std::vector<Marker> markers;

    bool played() const { return viewCount > 0; }
};
inline void from_json(const nlohmann::json& j, Item& r) {
    r.ratingKey = jstr(j, "ratingKey", jstr(j, "key"));
    r.key = jstr(j, "key");
    r.guid = jstr(j, "guid");
    r.type = jstr(j, "type");
    r.title = jstr(j, "title");
    r.summary = jstr(j, "summary");
    r.year = jint(j, "year");
    r.thumb = jstr(j, "thumb");
    r.art = jstr(j, "art");
    r.duration = jint(j, "duration");
    r.viewOffset = jint(j, "viewOffset");
    r.viewCount = jint(j, "viewCount");
    r.addedAt = jint(j, "addedAt");
    r.lastViewedAt = jint(j, "lastViewedAt");
    r.watchlistedAt = jint(j, "watchlistedAt");
    r.index = jint(j, "index");
    r.parentIndex = jint(j, "parentIndex");
    r.leafCount = jint(j, "leafCount");
    r.viewedLeafCount = jint(j, "viewedLeafCount");
    r.childCount = jint(j, "childCount");
    r.composite = jstr(j, "composite");
    r.smart = jbool(j, "smart");
    r.playlistType = jstr(j, "playlistType");
    r.contentRating = jstr(j, "contentRating");
    r.rating = jnum(j, "rating");
    r.audienceRating = jnum(j, "audienceRating");
    r.ratingImage = jstr(j, "ratingImage");
    r.audienceRatingImage = jstr(j, "audienceRatingImage");
    r.originallyAvailableAt = jstr(j, "originallyAvailableAt");
    r.parentRatingKey = jstr(j, "parentRatingKey");
    r.parentTitle = jstr(j, "parentTitle");
    r.parentThumb = jstr(j, "parentThumb");
    r.grandparentRatingKey = jstr(j, "grandparentRatingKey");
    r.grandparentTitle = jstr(j, "grandparentTitle");
    r.grandparentThumb = jstr(j, "grandparentThumb");
    r.grandparentArt = jstr(j, "grandparentArt");
    r.librarySectionID = jstr(j, "librarySectionID");
    r.librarySectionTitle = jstr(j, "librarySectionTitle");
    r.genres = jtags(j, "Genre");
    // cut-out logo: Image[{type:"clearLogo"}].url
    if (j.contains("Image") && j["Image"].is_array()) {
        for (auto& e : j["Image"]) {
            if (jstr(e, "type") == "clearLogo") r.clearLogo = jstr(e, "url");
        }
    }
    if (j.contains("Role") && j["Role"].is_array()) r.roles = j["Role"].get<std::vector<Role>>();
    if (j.contains("Director") && j["Director"].is_array()) r.directors = j["Director"].get<std::vector<Role>>();
    if (j.contains("Media") && j["Media"].is_array()) r.media = j["Media"].get<std::vector<Media>>();
    if (j.contains("Chapter") && j["Chapter"].is_array()) r.chapters = j["Chapter"].get<std::vector<Chapter>>();
    if (j.contains("Marker") && j["Marker"].is_array()) r.markers = j["Marker"].get<std::vector<Marker>>();
}

/// ---- Serialization for the offline cache (SPEC §4.1) -----------------------
/// to_json mirrors the exact JSON keys read by the matching from_json so a
/// fetched Item can be persisted to disk (meta/{ratingKey}.json) and re-read
/// identically offline. Nested overloads are declared before to_json(Item) so
/// that `j["Role"] = r.roles;` resolves them.
inline void to_json(nlohmann::json& j, const Stream& r) {
    j = nlohmann::json::object();
    j["id"] = r.id;
    j["streamType"] = r.streamType;
    j["index"] = r.index;
    j["codec"] = r.codec;
    j["language"] = r.language;
    j["languageTag"] = r.languageTag;
    j["displayTitle"] = r.displayTitle;
    j["selected"] = r.selected;
    j["default"] = r.isDefault;  // from_json reads isDefault from "default"
    j["forced"] = r.forced;
    j["channels"] = r.channels;
    j["key"] = r.key;
}

inline void to_json(nlohmann::json& j, const Part& r) {
    j = nlohmann::json::object();
    j["id"] = r.id;
    j["key"] = r.key;
    j["container"] = r.container;
    j["size"] = r.size;
    j["duration"] = r.duration;
    j["accessible"] = r.accessible;
    j["exists"] = r.exists;
    if (!r.streams.empty()) j["Stream"] = r.streams;
}

inline void to_json(nlohmann::json& j, const Media& r) {
    j = nlohmann::json::object();
    j["id"] = r.id;
    j["videoResolution"] = r.videoResolution;
    j["videoCodec"] = r.videoCodec;
    j["audioCodec"] = r.audioCodec;
    j["container"] = r.container;
    j["bitrate"] = r.bitrate;
    j["width"] = r.width;
    j["height"] = r.height;
    j["duration"] = r.duration;
    if (!r.parts.empty()) j["Part"] = r.parts;
}

inline void to_json(nlohmann::json& j, const Role& r) {
    j = nlohmann::json::object();
    j["id"] = r.id;
    j["tag"] = r.tag;
    j["role"] = r.role;
    j["thumb"] = r.thumb;
}

inline void to_json(nlohmann::json& j, const Chapter& r) {
    j = nlohmann::json::object();
    j["tag"] = r.tag;
    j["startTimeOffset"] = r.startTimeOffset;
    j["endTimeOffset"] = r.endTimeOffset;
}

inline void to_json(nlohmann::json& j, const Marker& r) {
    j = nlohmann::json::object();
    j["type"] = r.type;
    j["startTimeOffset"] = r.startTimeOffset;
    j["endTimeOffset"] = r.endTimeOffset;
}

inline void to_json(nlohmann::json& j, const Section& r) {
    j = nlohmann::json::object();
    j["key"] = r.key;
    j["type"] = r.type;
    j["title"] = r.title;
    j["uuid"] = r.uuid;
    j["composite"] = r.composite;
    j["art"] = r.art;
    j["hidden"] = r.hidden;
}

inline void to_json(nlohmann::json& j, const Item& r) {
    j = nlohmann::json::object();
    j["ratingKey"] = r.ratingKey;
    j["key"] = r.key;
    j["guid"] = r.guid;
    j["type"] = r.type;
    j["title"] = r.title;
    j["summary"] = r.summary;
    j["year"] = r.year;
    j["thumb"] = r.thumb;
    j["art"] = r.art;
    j["duration"] = r.duration;
    j["viewOffset"] = r.viewOffset;
    j["viewCount"] = r.viewCount;
    j["addedAt"] = r.addedAt;
    j["lastViewedAt"] = r.lastViewedAt;
    j["watchlistedAt"] = r.watchlistedAt;
    j["index"] = r.index;
    j["parentIndex"] = r.parentIndex;
    j["leafCount"] = r.leafCount;
    j["viewedLeafCount"] = r.viewedLeafCount;
    j["childCount"] = r.childCount;
    j["composite"] = r.composite;
    j["smart"] = r.smart;
    j["playlistType"] = r.playlistType;
    j["contentRating"] = r.contentRating;
    j["rating"] = r.rating;
    j["audienceRating"] = r.audienceRating;
    j["ratingImage"] = r.ratingImage;
    j["audienceRatingImage"] = r.audienceRatingImage;
    j["originallyAvailableAt"] = r.originallyAvailableAt;
    j["parentRatingKey"] = r.parentRatingKey;
    j["parentTitle"] = r.parentTitle;
    j["parentThumb"] = r.parentThumb;
    j["grandparentRatingKey"] = r.grandparentRatingKey;
    j["grandparentTitle"] = r.grandparentTitle;
    j["grandparentThumb"] = r.grandparentThumb;
    j["grandparentArt"] = r.grandparentArt;
    j["librarySectionID"] = r.librarySectionID;
    j["librarySectionTitle"] = r.librarySectionTitle;
    // cut-out logo: Image[{type:"clearLogo"}].url (mirrors from_json)
    if (!r.clearLogo.empty()) {
        nlohmann::json img = nlohmann::json::object();
        img["type"] = "clearLogo";
        img["url"] = r.clearLogo;
        j["Image"] = nlohmann::json::array({img});
    }
    // Genre/Director/Role/Media/Chapter/Marker: same wrapper arrays as from_json
    if (!r.genres.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& g : r.genres) {
            nlohmann::json tag = nlohmann::json::object();
            tag["tag"] = g;
            arr.push_back(tag);
        }
        j["Genre"] = arr;
    }
    if (!r.roles.empty()) j["Role"] = r.roles;
    if (!r.directors.empty()) j["Director"] = r.directors;
    if (!r.media.empty()) j["Media"] = r.media;
    if (!r.chapters.empty()) j["Chapter"] = r.chapters;
    if (!r.markers.empty()) j["Marker"] = r.markers;
}

/// Hub row (/hubs...)
struct Hub {
    std::string key;
    std::string hubIdentifier;  // e.g. home.continue, home.ondeck
    std::string title;
    std::string type;
    bool more = false;
    std::vector<Item> items;
};
inline void from_json(const nlohmann::json& j, Hub& r) {
    r.key = jstr(j, "key", jstr(j, "hubKey"));
    r.hubIdentifier = jstr(j, "hubIdentifier");
    r.title = jstr(j, "title");
    r.type = jstr(j, "type");
    r.more = jbool(j, "more");
    if (j.contains("Metadata") && j["Metadata"].is_array()) r.items = j["Metadata"].get<std::vector<Item>>();
}

/// MediaContainer envelope — equivalent of the Jellyfin Result<T> for
/// pagination (X-Plex-Container-Start/Size; total in `totalSize`/`size`).
template <typename T>
struct Container {
    std::vector<T> Items;
    long TotalRecordCount = 0;
    long StartIndex = 0;
};

template <typename T>
inline void from_json(const nlohmann::json& j, Container<T>& r) {
    const nlohmann::json& mc = j.contains("MediaContainer") ? j.at("MediaContainer") : j;
    for (const char* key : {"Metadata", "Directory", "Hub", "SearchResult"}) {
        auto it = mc.find(key);
        if (it == mc.end() || !it->is_array()) continue;
        if (strcmp(key, "SearchResult") == 0) {
            // /library/search wraps each result: SearchResult[].Metadata
            for (auto& e : *it)
                if (e.contains("Metadata")) r.Items.push_back(e.at("Metadata").get<T>());
        } else {
            r.Items = it->get<std::vector<T>>();
        }
        break;
    }
    r.StartIndex = (long)jint(mc, "offset");
    r.TotalRecordCount = (long)jint(mc, "totalSize", jint(mc, "size", r.Items.size()));
}

}  // namespace plex
