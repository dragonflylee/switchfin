/*
    pleNx — modèles natifs de l'API Plex.
    Spécification vérifiée : PLEX_MIGRATION.md §2 (extraite de plezy, citations fichier:ligne).

    Unités : durées/positions en MILLISECONDES, horodatages en SECONDES epoch,
    offset de seek du transcodeur en SECONDES entières (cf. §2.6).
*/

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace plex {

/// ---- plex.tv (authentification & découverte) -------------------------------
const std::string tvApiBase = "https://clients.plex.tv/api/v2";
const std::string tvApiFallback = "https://plex.tv/api/v2";
// PIN « faible » (4 caractères) : requis pour la saisie manuelle sur
// https://plex.tv/link. `?strong=true` (plezy) génère un code long destiné
// à être passé en paramètre d'URL à app.plex.tv/auth, pas à être tapé.
const std::string_view tvApiPins = "{}/pins";
const std::string_view tvApiPinPoll = "{}/pins/{}";
const std::string_view tvApiUser = "{}/user";
const std::string_view tvApiResources = "{}/resources?includeHttps=1&includeRelay=1&includeIPv6=1";
const std::string_view tvApiHomeUsers = "{}/home/users";
const std::string_view tvApiSwitchUser = "{}/home/users/{}/switch?{}";
// URL à présenter à l'utilisateur (téléphone/navigateur)
const std::string tvLinkUrl = "https://plex.tv/link";

/// ---- Provider plex.tv (Watchlist du COMPTE) ---------------------------------
/// La watchlist appartient au compte plex.tv, pas au serveur : ces hôtes se
/// requêtent avec le token COMPTE (AppConfig::getAccountToken()), jamais le
/// token serveur. plezy n'implémente pas la watchlist — endpoints de l'API
/// provider officielle (Plex Web/python-plexapi), TOUS vérifiés en GET réel
/// le 2026-06-10 sur le compte de l'utilisateur (réponses JSON constatées).
const std::string discoverApiBase = "https://discover.provider.plex.tv";
const std::string metadataApiBase = "https://metadata.provider.plex.tv";
/// Liste paginée (X-Plex-Container-*) ; `sort=watchlistedAt:desc` vérifié
/// (asc/desc inversent bien l'ordre). Metadata[] : ratingKey PROVIDER,
/// guid plex://movie|show/…, thumb/art en URLs ABSOLUES (tmdb,
/// metadata-static.plex.tv — tailles originales, à proxifier).
const std::string_view apiWatchlistAll = "/library/sections/watchlist/all?{}";
/// PUT, ratingKey = ratingKey PROVIDER (dernier segment du guid plex://…)
const std::string_view apiWatchlistAdd = "/actions/addToWatchlist?ratingKey={}";
const std::string_view apiWatchlistRemove = "/actions/removeFromWatchlist?ratingKey={}";
/// État watchlist d'un titre : le metadata SERVEUR n'expose aucun champ
/// (vérifié) ; sur metadata.provider avec includeUserState=1, le champ
/// `watchlistedAt` (epoch s) est présent ⇔ le titre est dans la watchlist
/// (vérifié sur un film watchlisté et un film non watchlisté).
const std::string_view apiProviderMetadata = "/library/metadata/{}?includeUserState=1";
/// Correspondance provider → serveur : items du serveur portant ce guid
/// (vérifié : GET /library/all?guid=plex%3A%2F%2Fmovie%2F… → Metadata[1]).
const std::string_view apiLibraryMatch = "/library/all?{}";

/// ---- Serveur de médias (PMS) ------------------------------------------------
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
/// filmographie d'une personne (plex_client.dart:2509-2511)
const std::string_view apiPersonMedia = "/library/people/{}/media?{}";
const std::string_view apiGrandchildren = "/library/metadata/{}/grandchildren?{}";
/// tous les épisodes d'une série, toutes saisons confondues (Container<Item>)
const std::string_view apiAllLeaves = "/library/metadata/{}/allLeaves";
const std::string_view apiExtras = "/library/metadata/{}/extras";
const std::string_view apiSearch = "/library/search?{}";
const std::string_view apiCollections = "/library/sections/{}/collections?{}";
const std::string_view apiCollectionChildren = "/library/collections/{}/children?{}";
/// listes de lecture (vérifié serveur 2026-06-10 : Metadata[] type "playlist",
/// composite/leafCount/duration/smart ; pagination X-Plex-Container-* supportée)
const std::string_view apiPlaylists = "/playlists?{}";
const std::string_view apiPlaylistItems = "/playlists/{}/items?{}";
const std::string_view apiScrobble = "/:/scrobble?key={}&identifier=com.plexapp.plugins.library";
const std::string_view apiUnscrobble = "/:/unscrobble?key={}&identifier=com.plexapp.plugins.library";
const std::string_view apiTimeline = "/:/timeline?{}";
const std::string_view apiPhotoTranscode = "/photo/:/transcode?{}";
const std::string_view apiTranscodeDecision = "/video/:/transcode/universal/decision?{}";
const std::string_view apiTranscodeStart = "/video/:/transcode/universal/start?{}";
const std::string_view apiPartIndexes = "/library/parts/{}/indexes/sd";

/// Types numériques (paramètre `type=` ; plex_constants.dart:19-31)
const int typeMovie = 1;
const int typeShow = 2;
const int typeSeason = 3;
const int typeEpisode = 4;

/// Types texte (champ `type` des Metadata)
const std::string mediaTypeMovie = "movie";
const std::string mediaTypeShow = "show";
const std::string mediaTypeSeason = "season";
const std::string mediaTypeEpisode = "episode";
const std::string mediaTypeClip = "clip";
const std::string mediaTypeCollection = "collection";
const std::string mediaTypePhoto = "photo";
const std::string mediaTypePlaylist = "playlist";

/// streamType (plex_constants.dart:7-11)
const int streamTypeVideo = 1;
const int streamTypeAudio = 2;
const int streamTypeSubtitle = 3;

/// ---- Helpers JSON tolérants -------------------------------------------------
/// Plex omet les champs absents et renvoie parfois des nombres sous forme de
/// chaîne ; ces helpers absorbent les deux cas sans lever d'exception.
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

/// Tableaux d'objets `[{"tag": "..."}]` (Genre, Director, Writer…)
inline std::vector<std::string> jtags(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (auto& e : *it) out.push_back(jstr(e, "tag"));
    return out;
}

/// ---- Authentification (plex.tv) ---------------------------------------------

/// POST /api/v2/pins?strong=true (plex_auth_service.dart:129-135)
struct PinResult {
    int64_t id = 0;
    std::string code;
    std::string authToken;  // rempli quand l'utilisateur a validé sur plex.tv/link
};
inline void from_json(const nlohmann::json& j, PinResult& r) {
    r.id = jint(j, "id");
    r.code = jstr(j, "code");
    r.authToken = jstr(j, "authToken");
}

/// GET /api/v2/user (plex_auth_service.dart:224-228)
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
    bool isProtected = false;  // `protected` : PIN requis pour /switch
    bool admin = false;
};
inline void from_json(const nlohmann::json& j, HomeUser& r) {
    r.uuid = jstr(j, "uuid");
    r.title = jstr(j, "title");
    r.thumb = jstr(j, "thumb");
    r.isProtected = jbool(j, "protected");
    r.admin = jbool(j, "admin");
}

/// Une connexion candidate vers un serveur (resources → connections[])
struct Connection {
    std::string protocol;  // "http" | "https"
    std::string address;
    int port = 32400;
    std::string uri;  // ex. https://192-168-1-100.<hash>.plex.direct:32400
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

/// GET /api/v2/resources, filtré sur provides=server (plex_auth_service.dart:309-360)
struct ServerResource {
    std::string name;
    std::string clientIdentifier;  // machine id du serveur
    std::string accessToken;       // token PROPRE à ce serveur (≠ token de compte)
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

/// ---- Modèles de médias --------------------------------------------------------

/// Bibliothèque (Directory de /library/sections)
struct Section {
    std::string key;   // id numérique de la section (en chaîne)
    std::string type;  // movie | show | artist | photo
    std::string title;
    std::string uuid;
    std::string composite;  // mosaïque d'affiches générée par le serveur
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
    int streamType = 0;  // 1 vidéo, 2 audio, 3 sous-titre
    int64_t index = -1;
    std::string codec;
    std::string language;
    std::string languageTag;
    std::string displayTitle;
    bool selected = false;
    bool isDefault = false;
    bool forced = false;
    int channels = 0;
    std::string key;  // sous-titre externe : chemin sidecar (/library/streams/{id})
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
    std::string key;  // chemin de lecture directe : {base}{key}?X-Plex-Token=…
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
    std::string videoResolution;  // "1080", "4k"…
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
    std::string tag;            // titre du chapitre
    int64_t startTimeOffset = 0;  // ms
    int64_t endTimeOffset = 0;    // ms
};
inline void from_json(const nlohmann::json& j, Chapter& r) {
    r.tag = jstr(j, "tag");
    r.startTimeOffset = jint(j, "startTimeOffset");
    r.endTimeOffset = jint(j, "endTimeOffset");
}

/// Marqueur intro/générique (type = "intro" | "credits" ; ms)
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
    std::string id;    // identifiant numérique (fiche personne, filmographie)
    std::string tag;   // nom de la personne
    std::string role;  // personnage
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

/// Item de médiathèque (objet `Metadata` ; plex_mappers.dart:551-724)
struct Item {
    std::string ratingKey;  // identifiant ⇔ Jellyfin Item.Id
    std::string key;        // chemin de détail (/library/metadata/{ratingKey})
    std::string guid;
    std::string type;  // movie | show | season | episode | clip | collection
    std::string title;
    std::string summary;
    int64_t year = 0;
    std::string thumb;      // chemin relatif d'affiche
    std::string art;        // chemin relatif de fond
    std::string clearLogo;  // logo détouré (tableau Image[], type clearLogo)
    int64_t duration = 0;    // ms
    int64_t viewOffset = 0;  // ms — position de reprise
    int64_t viewCount = 0;   // > 0 ⇔ vu
    int64_t addedAt = 0;     // epoch s
    int64_t lastViewedAt = 0;
    int64_t watchlistedAt = 0;  // epoch s — metadata.provider + includeUserState=1 (0 = absent)
    int64_t index = 0;        // n° d'épisode (ou de saison sur une saison)
    int64_t parentIndex = 0;  // n° de saison (sur un épisode)
    int64_t leafCount = 0;    // nb d'épisodes (série/saison)
    int64_t viewedLeafCount = 0;
    int64_t childCount = 0;
    // playlists (type "playlist" ; GET /playlists?playlistType=video)
    std::string composite;     // mosaïque 1:1 générée par le serveur
    bool smart = false;        // playlist intelligente (filtre dynamique)
    std::string playlistType;  // video | audio | photo — seules les video sont lisibles ici
    std::string contentRating;
    double rating = 0.0;          // note critique 0-10
    double audienceRating = 0.0;  // note public 0-10
    std::string originallyAvailableAt;  // YYYY-MM-DD
    std::string parentRatingKey;        // saison (depuis un épisode)
    std::string parentTitle;
    std::string parentThumb;
    std::string grandparentRatingKey;  // série (depuis un épisode)
    std::string grandparentTitle;
    std::string grandparentThumb;
    std::string grandparentArt;
    std::vector<std::string> genres;
    std::vector<Role> roles;
    std::vector<Role> directors;  // Director : même forme que Role (id/tag/thumb)
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
    r.originallyAvailableAt = jstr(j, "originallyAvailableAt");
    r.parentRatingKey = jstr(j, "parentRatingKey");
    r.parentTitle = jstr(j, "parentTitle");
    r.parentThumb = jstr(j, "parentThumb");
    r.grandparentRatingKey = jstr(j, "grandparentRatingKey");
    r.grandparentTitle = jstr(j, "grandparentTitle");
    r.grandparentThumb = jstr(j, "grandparentThumb");
    r.grandparentArt = jstr(j, "grandparentArt");
    r.genres = jtags(j, "Genre");
    // logo détouré : Image[{type:"clearLogo"}].url (plex_mappers.dart:742-764)
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

/// Rangée de hub (/hubs…)
struct Hub {
    std::string key;
    std::string hubIdentifier;  // ex. home.continue, home.ondeck
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

/// Enveloppe MediaContainer — équivalent du Result<T> Jellyfin pour la
/// pagination (X-Plex-Container-Start/Size ; total dans `totalSize`/`size`).
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
            // /library/search enveloppe chaque résultat : SearchResult[].Metadata
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
