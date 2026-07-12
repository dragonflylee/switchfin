/*
    GMCA — Plex-specific constants and auth models.
    The neutral media model now lives in api/media/types.hpp; this file keeps
    only what is specific to the Plex protocol (plex.tv endpoints, PMS endpoints,
    numeric type codes, PIN/resources auth structs).

    A bridge of `using media::...` declarations is kept below so existing code
    referring to `plex::Item`, `plex::Container`, etc. keeps compiling while call
    sites migrate to the media:: namespace (MULTI_BACKEND.md §8, Phase 1).

    Units: durations/positions in MILLISECONDS, timestamps in epoch SECONDS,
    transcoder seek offset in whole SECONDS (cf. PLEX_MIGRATION.md §2.6).
*/

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "api/media/types.hpp"

namespace plex {

/// ---- Bridge to the neutral media model (transitional) ----------------------
using media::Chapter;
using media::Container;
using media::Hub;
using media::Item;
using media::Marker;
using media::Media;
using media::Part;
using media::Role;
using media::Section;
using media::Stream;
// generic JSON helpers
using media::jbool;
using media::jint;
using media::jnum;
using media::jstr;
using media::jtags;
// generic media type strings
using media::mediaTypeAlbum;
using media::mediaTypeArtist;
using media::mediaTypeClip;
using media::mediaTypeCollection;
using media::mediaTypeEpisode;
using media::mediaTypeMovie;
using media::mediaTypePhoto;
using media::mediaTypePlaylist;
using media::mediaTypeSeason;
using media::mediaTypeShow;
using media::mediaTypeTrack;
// normalized streamType values
using media::streamTypeAudio;
using media::streamTypeSubtitle;
using media::streamTypeVideo;

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
/// Music (audio) universal transcoder — mirrors the video endpoints (issue #11).
/// Best-effort / UNVERIFIED against a live server (see backend.cpp resolvePlayback).
const std::string_view apiMusicTranscodeDecision = "/music/:/transcode/universal/decision?{}";
const std::string_view apiMusicTranscodeStart = "/music/:/transcode/universal/start.mp3?{}";
/// Tears a transcode session down server-side (frees the transcoder). Must be
/// called on every (re)start and on player exit or sessions pile up orphaned.
const std::string_view apiTranscodeStop = "/video/:/transcode/universal/stop?{}";
const std::string_view apiPartIndexes = "/library/parts/{}/indexes/sd";

/// Numeric types (`type=` parameter) — Plex-specific encoding
const int typeMovie = 1;
const int typeShow = 2;
const int typeSeason = 3;
const int typeEpisode = 4;
const int typeArtist = 8;
const int typeAlbum = 9;
const int typeTrack = 10;

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

}  // namespace plex
