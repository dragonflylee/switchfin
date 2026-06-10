/*
    pleNx — Watchlist du compte plex.tv (discover/metadata.provider.plex.tv).

    Fonctionnalité de COMPTE : toutes les requêtes provider utilisent le token
    plex.tv du profil actif (AppConfig::getAccountToken()), PAS le token
    serveur. Les items renvoyés par le provider sont des métadonnées
    « universelles » (ratingKey provider, guid plex://movie|show/…, images en
    URLs absolues) qui peuvent ne pas exister sur le serveur de l'utilisateur :
    la correspondance se fait par guid (matchInLibrary).

    plezy n'implémente pas la watchlist (seul discover_screen.dart:1017-1018 y
    fait allusion pour une icône) ; les endpoints sont ceux de l'API provider
    officielle (Plex Web / python-plexapi), vérifiés en GET réel le 2026-06-10
    — détail des constats dans api/plex/types.hpp (section Provider plex.tv).
*/

#pragma once

#include <memory>
#include <unordered_set>

#include "api/plex.hpp"

namespace plex {

/// ratingKey PROVIDER d'un guid Plex : « plex://movie/5d77… » → « 5d77… ».
/// Chaîne vide si le guid ne vient pas de l'agent Plex (ancien agent TMDB…) :
/// dans ce cas le titre n'est pas adressable sur le provider.
inline std::string providerRatingKey(const std::string& guid) {
    if (guid.rfind("plex://", 0) != 0) return "";
    auto pos = guid.rfind('/');
    if (pos == std::string::npos || pos + 1 >= guid.size()) return "";
    return guid.substr(pos + 1);
}

/// La watchlist Plex n'accepte que films et séries (pas d'épisodes/saisons :
/// les guids plex://episode/… existent mais addToWatchlist les refuse côté
/// Plex Web également — l'action n'y est proposée que sur movie/show).
inline bool watchlistable(const Item& item) {
    return (item.type == mediaTypeMovie || item.type == mediaTypeShow) && !providerRatingKey(item.guid).empty();
}

/// Tri par défaut de la watchlist : plus récemment ajouté en premier.
const std::string watchlistDefaultSort = "watchlistedAt:desc";

/// Liste paginée de la watchlist. `then` reçoit le Container provider :
/// Items[].ratingKey est le ratingKey PROVIDER, thumb une URL absolue.
///
/// Paramètres VÉRIFIÉS en GET réel sur discover.provider (2026-06-10) :
///  - sort : watchlistedAt, titleSort, originallyAvailableAt (suffixes :asc et
///    :desc honorés pour les trois). `title` et `year` sont IGNORÉS en
///    silence (ordre par défaut renvoyé) — ne pas les exposer. `rating:desc`
///    répond mais l'ordre observé n'est pas exploitable (paquets d'ex æquo).
///  - type : 1 (films) | 2 (séries) filtre bien (714/167 sur 884 constatés) ;
///    `libtype=movie|show` (python-plexapi) est IGNORÉ sur cet endpoint.
///  - X-Plex-Container-Size : plafonné à 100 (400 « Invalid value » au-delà).
/// `sort` vide → watchlistDefaultSort ; `type` ∉ {typeMovie, typeShow} → tous.
inline void fetchWatchlist(size_t start, size_t size, const std::string& sort, int type,
    const std::function<void(Container<Item>)>& then, OnError error) {
    HTTP::Form query = {{"sort", sort.empty() ? watchlistDefaultSort : sort}};
    if (type == typeMovie || type == typeShow) query["type"] = std::to_string(type);
    addPagination(query, start, size);
    getJSON<Container<Item>>(discoverApiBase, AppConfig::instance().getAccountToken(), then, error, apiWatchlistAll,
        HTTP::encode_form(query));
}

/// État « est dans la watchlist » d'un titre identifié par son guid plex://….
/// Le metadata serveur n'expose rien (vérifié) : on interroge le provider —
/// `watchlistedAt` présent ⇔ watchlisté. `then(false)` si guid non provider.
inline void fetchWatchlistState(const std::string& guid, const std::function<void(bool)>& then, OnError error) {
    std::string key = providerRatingKey(guid);
    if (key.empty()) {
        if (then) then(false);
        return;
    }
    getJSON<Container<Item>>(
        metadataApiBase, AppConfig::instance().getAccountToken(),
        [then](const Container<Item>& r) {
            if (then) then(!r.Items.empty() && r.Items.front().watchlistedAt > 0);
        },
        error, apiProviderMetadata, key);
}

/// Ajoute (add=true) ou retire (add=false) un titre de la watchlist du compte.
/// PUT /actions/addToWatchlist|removeFromWatchlist?ratingKey={provider}.
inline void setWatchlisted(const std::string& guid, bool add, const std::function<void()>& then, OnError error) {
    std::string key = providerRatingKey(guid);
    if (key.empty()) {
        if (error) error("invalid guid");
        return;
    }
    putAction(discoverApiBase, AppConfig::instance().getAccountToken(), then, error,
        add ? apiWatchlistAdd : apiWatchlistRemove, key);
}

/// Correspondance provider → serveur actif : items de la bibliothèque portant
/// ce guid (GET /library/all?guid=… vérifié). Container vide = pas en
/// bibliothèque ; sinon Items[0] est l'item SERVEUR (ratingKey local).
inline void matchInLibrary(
    const std::string& guid, const std::function<void(Container<Item>)>& then, OnError error) {
    auto& conf = AppConfig::instance();
    getJSON<Container<Item>>(
        conf.getUrl(), conf.getToken(), then, error, apiLibraryMatch, HTTP::encode_form({{"guid", guid}}));
}

/// Ensemble des guid de TOUTE la bibliothèque du serveur actif (sections
/// movie et show) — lookup O(1) « ce titre watchlisté est-il sur le serveur ? ».
///
/// Constats serveur (Babylon, 2026-06-10) :
///  - le champ `guid` (plex://movie|show/…) est présent dans le listing
///    STANDARD de /library/sections/{id}/all — aucun paramètre requis
///    (includeGuids ne concerne que les tags Guid[] externes tmdb/imdb) ;
///  - volumétrie : 6613 films + 232 séries = 6845 guids ; listing brut
///    ≈ 2,7 Ko/item (~18 Mo au total) → réponse AMINCIE via excludeFields +
///    excludeElements (~0,5 Ko/item, les éléments Image et UltraBlurColors
///    ne sont pas excluables) et PAGINÉE par 1000 pour borner chaque parse ;
///  - mesuré (curl, LAN) : 6845 items / ~3,3 Mo / ~3-5 s en 8 pages de 1000.
///
/// Tout est requêté dans UN brls::async (boucles getSync synchrones), puis
/// `then(set)` est rappelé sur le thread UI. En cas d'échec réseau, `error`
/// est rappelé et l'appelant doit dégrader (pas de set = présence inconnue).
inline void fetchLibraryGuids(
    const std::function<void(std::shared_ptr<std::unordered_set<std::string>>)>& then, OnError error) {
    auto& conf = AppConfig::instance();
    std::string base = conf.getUrl();
    std::string token = conf.getToken();
    brls::async([base, token, then, error]() {
        try {
            auto guids = std::make_shared<std::unordered_set<std::string>>();
            auto sections = getSync(base + std::string(apiSections), token).get<Container<Section>>();
            for (auto& section : sections.Items) {
                if (section.type != "movie" && section.type != "show") continue;
                const size_t pageSize = 1000;
                size_t start = 0;
                while (true) {
                    HTTP::Form query = {
                        // réponse minimale : seuls guid/ratingKey/type restent
                        // (plus Image/UltraBlurColors, non excluables)
                        {"excludeFields",
                            "summary,tagline,art,thumb,studio,contentRating,originalTitle,audienceRatingImage,"
                            "ratingImage,primaryExtraKey,key,originallyAvailableAt,addedAt,updatedAt,duration,"
                            "rating,audienceRating,year,title,titleSort,slug,theme,banner,index,childCount,"
                            "leafCount,viewedLeafCount,contentRatingAge,mediaCount,mediaCountOptimized"},
                        {"excludeElements", "Media,Genre,Country,Director,Writer,Role,Collection,Image,"
                                            "UltraBlurColors,Guid,Label"},
                    };
                    addPagination(query, start, pageSize);
                    auto r = getSync(base + fmt::format(fmt::runtime(apiSectionAll), section.key,
                                                HTTP::encode_form(query)),
                        token)
                                 .get<Container<Item>>();
                    for (auto& item : r.Items)
                        if (!item.guid.empty()) guids->insert(item.guid);
                    start += r.Items.size();
                    if (r.Items.empty() || (long)start >= r.TotalRecordCount) break;
                }
            }
            brls::sync(std::bind(then, std::move(guids)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

}  // namespace plex
