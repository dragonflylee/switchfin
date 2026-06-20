# pleNx — Architecture multi-backend (Plex · Jellyfin/Emby · Stremio)

> **But** : permettre de connecter pleNx à un serveur **Plex**, **Emby/Jellyfin** ou **Stremio**, en
> conservant une **interface/expérience identique** quel que soit le type de serveur. Seuls les
> **actions, menus et onglets** varient, pilotés par les **capacités** du backend connecté.
>
> Branche : `feat/multi-backend`. Document produit le 2026-06-16 à partir d'une cartographie exhaustive
> de l'intégration Plex existante et d'une analyse du protocole Stremio (citations `fichier:ligne`).
> Complète `PLEX_MIGRATION.md` (qui documente l'API Jellyfin d'origine, §1, et l'API Plex, §2).

## 0. État de départ & contrainte

pleNx est un ex-client Jellyfin (fork wiliwili) **migré vers Plex** (`PLEX_MIGRATION.md`). La décision D1
a rendu tout le code « nativement Plex » : les ~25 sites d'appel UI manipulent directement `plex::Item`,
`plex::Container<T>`, des `ratingKey`, des durées en ms, et **formatent les URLs Plex sur place**
(`plex::getJSON(conf.getUrl(), conf.getToken(), then, error, endpoint, args...)`).

Le projet contient déjà le **patron d'abstraction** à généraliser : `remote::Client`
(`app/include/client/client.hpp:39-50`) — classe de base abstraite (dtor virtuel + méthode virtuelle
pure `list()`) + factory `remote::create(AppRemote)` dispatchant par schéma d'URL
(`app/src/client/client.cpp:9-26`). C'est le modèle direct de l'interface `media::Backend`.

**Décisions (2026-06-16)** :
- **Périmètre de cette branche** : Phase 1 (fondation) + Phase 2 (Jellyfin/Emby). Stremio = phase 3.
- **Stremio** (phase 3) : connexion via **compte `api.strem.io`** (synchro addons + bibliothèque).
  Torrents (`infoHash`) → streaming server local `127.0.0.1:11470` → **desktop uniquement**.
- **Modèle pivot** : renommer `plex::` → **`media::`** dès la fondation.

---

## 1. Vue d'ensemble — 3 couches

```
        UI (tabs/, view/, activity/)  ── inchangée, consomme media::Item / Container<T>
                       │
                       ▼  AppConfig::instance().backend()           (le backend ACTIF)
        ┌──────────────────────────────────────────────┐
        │   media::Backend  (interface abstraite)        │   api/backend.hpp
        │   + media::Capabilities (descripteur)          │
        └──────────────────────────────────────────────┘
            ▲                ▲                 ▲
   PlexBackend      JellyfinBackend     StremioBackend         api/plex/, api/jellyfin/, api/stremio/
   (X-Plex-*,        (Authorization:     (addons + compte
    MediaContainer)   MediaBrowser,       api.strem.io)
                      ticks→ms)
            │                │                 │
            └────────────────┴─────────────────┘
                       │  produisent tous des media::* (modèle pivot neutre)
                       ▼
        media::Item / Container / Section / Hub / Media / Part / Stream / Chapter / Marker / Role
                                                                            api/media/types.hpp
                       │
                       ▼
        HTTP  (libcurl wrapper, verbes + options par tags)                  api/http.hpp  (inchangé)
```

**Principe clé — séparation modèle / mapping.** Le modèle `media::*` est **neutre** (aucun `from_json`
couplé à un fournisseur). Le `from_json` Plex actuel (`plex/types.hpp:433-482`, qui lit `ratingKey`,
`viewOffset`, `MediaContainer`…) **n'est PAS le modèle** : c'est le *mapping Plex* et il migre dans
`PlexBackend`. Jellyfin aura son propre mapping (`Items`/ticks/`Authorization`), Stremio le sien
(`meta`/`videos[]`/ids IMDB). Tous produisent le même `media::Item`.

---

## 2. Modèle pivot neutre (`media::`)

Réutilise les structs existants (renommés du namespace `plex` vers `media`), **dépouillés de leur
`from_json`** : `Item`, `Container<T>`, `Section`, `Hub`, `Media`, `Part`, `Stream`, `Chapter`,
`Marker`, `Role`. Champs inchangés (cf. `plex/types.hpp:246-528`).

Précisions sur l'identité, déjà présentes et à conserver :
- `Item::ratingKey` = identifiant opaque **propre au backend** (Plex ratingKey, Jellyfin Id, Stremio
  `tt…[:s:e]`). Renommage interne possible en `id`, mais `ratingKey` est acceptable comme « id opaque ».
- `Item::guid` = identité **cross-source** (Plex `plex://…`, Jellyfin ProviderIds, Stremio = l'id IMDB
  lui-même). Sert la watchlist/match (`watchlist.hpp:97`).
- Unités pivot : **durées/positions en ms**, **horodatages en epoch s** (cf. `PLEX_MIGRATION.md §2.6`).
  Chaque backend convertit (Jellyfin : ticks ÷ 10 000 = ms ; Stremio : `runtime`="120m" à parser).

Les helpers JSON lenients (`jstr/jint/jnum/jbool/jtags`, `plex/types.hpp:115-165`) sont **génériques** :
ils restent disponibles pour tous les mappers (déplacés en `api/json_util.hpp` ou conservés dans
`media::`). Ils ne sont pas spécifiques à Plex.

---

## 3. Interface `media::Backend` (api/backend.hpp)

Signatures exactes : voir le header. Conventions :
- **Async, style existant** : `Then<T> = std::function<void(T)>`, `OnError = std::function<void(const
  std::string&)>`. Chaque verbe lance `brls::async`, parse, resynchronise via `brls::sync` (comme
  `plex::getJSON`, `plex.hpp:74-86`). Le backend encapsule base URL + token + headers.
- **`resolvePlayback` est synchrone** (appelée dans un contexte `async` par le player), retourne un
  `PlaybackSource{url, extraMpvOptions, isTranscode, playMethod, startSeconds}` (cf. §6).
- Les verbes **gated par capacité** ont une implémentation par défaut no-op/erreur dans la base ; un
  backend ne les surcharge que s'il déclare la capacité.

### Verbes (mappés 1:1 sur l'inventaire des sites d'appel)

| Verbe | Retour | Site d'appel témoin (avant) | Endpoint Plex actuel |
|---|---|---|---|
| `listSections` | `Container<Section>` | `main_activity.cpp:14` | `/library/sections` |
| `getHomeHubs(count, excludeContinue)` | `Container<Hub>` | `home_tab.cpp:81` | `/hubs?…` |
| `getSectionHubs(sectionId, count)` | `Container<Hub>` | `suggest_movie.cpp:50` | `/hubs/sections/{}` |
| `getContinueWatching(count)` | `Container<Hub>` | `home_tab.cpp:46` | `/hubs/continueWatching` |
| `getLibraryGrid(sectionId, GridQuery, start, size)` | `Container<Item>` | `media_collection.cpp:336` | `/library/sections/{}/all?…` |
| `getCollectionChildren(id, start, size)` | `Container<Item>` | `media_collection.cpp:336` (branche) | `/library/collections/{}/children` |
| `getHubPage(hubKey, start, size)` | `Container<Item>` | `hub_view.cpp:56` | clé de hub brute |
| `getItemDetail(id, full)` | `Item` | `media_movie.cpp:166`, `media_series.cpp:503` | `/library/metadata/{}?includes` |
| `getChildren(id)` | `Container<Item>` | `media_series.cpp:578` (saisons), `:241` (épisodes) | `/library/metadata/{}/children` |
| `getAllEpisodes(showId, includeStreams)` | `Container<Item>` | `player_view.cpp:140`, `media_series.cpp:415` | `/library/metadata/{}/grandchildren`/`allLeaves` |
| `getNextUp(showId)` | `Item` | `media_series.cpp:609` | `?includeOnDeck=1` |
| `getExtras(id)` | `Container<Item>` | `media_series.cpp:696` | `/library/metadata/{}/extras` |
| `getRelated(id, count)` | `Container<Hub>` | `media_movie.cpp:267` | `/hubs/metadata/{}/related` |
| `getPersonMedia(personId, count)` | `Container<Item>` | `media_person.cpp:39` | `/library/people/{}/media` |
| `search(query, kinds, limit)` | `Container<Item>` | `search_tab.cpp:353`, `search_list.cpp:36` | `/library/search?…` |
| `getRecentlyAdded(start, size)` | `Container<Item>` | `search_tab.cpp:329` | `/library/recentlyAdded` |
| `getGenres(sectionId, type)` | `Container<Section>` | `media_collection.cpp:79` | `/library/sections/{}/genre` |
| `getCollections(sectionId, start, size)` | `Container<Item>` | `media_collection.cpp:115` | `/library/sections/{}/collections` |
| `getPlaylists(start, size)` | `Container<Item>` | `playlists_tab.cpp:98` | `/playlists?playlistType=video` |
| `getPlaylistItems(id, start, size)` | `Container<Item>` | `playlist_view.cpp:71` | `/playlists/{}/items` |
| `markWatched(id)` / `markUnwatched(id)` | — | `context_menu.cpp:252/264`, `player_view.cpp:402` | `/:/scrobble` / `/:/unscrobble` |
| `resolvePlayback(item, PlaybackOptions)` | `PlaybackSource` | `player_view.cpp:191-362` | decision + start / Part.key |
| `reportProgress(id, state, posMs, durMs)` | — | `player_view.cpp:364` | `/:/timeline` |
| `imageUrl(path, w, h)` | `std::string` | `image.hpp:19`, `watchlist_tab.cpp:21`, `genre_image.cpp:151` | `/photo/:/transcode` ou direct |
| `downloadUrl(partKey)` | `std::string` | `download.cpp:225` | `{Part.key}?download=1` |

### Verbes gated par capacité (watchlist Plex / favoris Jellyfin / library Stremio)
`listWatchlist`, `getWatchlistState(guid)→bool`, `setWatchlisted(guid, add)`, `matchInLibrary(guid)→Item`,
`fetchLibraryGuids()→set` (`watchlist.hpp`). Côté Jellyfin, le menu « Favori » mappe sur
`POST/DELETE /Users/{u}/FavoriteItems/{id}` ; côté Stremio, sur un `LibraryItem temp`. L'UI n'expose
qu'un seul affordance « ajouter à ma liste », routé selon `caps`.

### Structs de paramètres
- `GridQuery{ sortField, descending, unwatchedOnly, genreId, mediaType }` — abstrait
  `sort=field[:desc]`, `unwatched=1`, `genre={id}`, `type=1|2` (`media_collection.cpp:336-404`).
  Capacité `serverSort`/`serverFilter` : si absente (Stremio), l'UI masque le sélecteur de tri.
- `MediaKind { Movie, Show, Season, Episode, Collection, Playlist, … }` — remplace les `type=1|2` et
  `searchTypes=movies,tv` numériques/textuels.
- `PlaybackOptions{ seekMs, audioStreamId, subtitleStreamId, bitrateCap, burnSubtitles, videoCodec }`.
- `PlaybackSource{ url, mpvExtra, isTranscode, playMethod, startSeconds }`.

---

## 4. `media::Capabilities` — pilote UI

Struct de flags porté par chaque backend (`backend().caps()`). Points de branchement UI :

| Flag | Effet UI | Plex | JF/Emby | Stremio |
|---|---|---|---|---|
| `sections` | onglets bibliothèque dynamiques (`main_activity.cpp:11-57`) | ✓ | ✓ | ✓ (catalogues) |
| `homeHubs` / `continueWatching` | rangées d'accueil (`home_tab`) | ✓ | ✓ composé | ✓ |
| `serverSort` / `serverFilter` | sélecteur tri/filtre des grilles (`media_filter`) | ✓ | ✓ | ✗ (masqué) |
| `genres` | onglet/filtre genres | ✓ | ✓ | ~ |
| `collections` | onglet Collections | ✓ | ✓ | ✗ |
| `playlists` | onglet Playlists (`main.xml:32`) | ✓ | ✓ | ✗ |
| `related` / `personPages` | rangées similaires / fiche personne | ✓ / ~ | ✓ / ✓ | ~ / ✗ |
| `globalSearch` | recherche globale vs par catalogue | ✓ | ✓ | ~ |
| `markWatched` | menu « marquer vu/non-vu » | ✓ | ✓ | ✓ |
| `favorites` | menu « Favori » | ✗ | ✓ | ~ |
| `watchlist` | onglet Watchlist (`main.xml:39`) + menu | ✓ | ✗ | ✓ |
| `skipIntro` | bouton passer l'intro (markers — non branché aujourd'hui) | ✓ | ✓ | ✗ |
| `transcode` | menu qualité / décision transcode | ✓ | ✓ | ✗ |
| `serverProgress` | report progression serveur (sinon local) | ✓ | ✓ | ~ (compte) |
| `downloadOriginal` | bouton télécharger | ✓ | ✓ | ~ |
| `multiProfile` | sélection de profil (Plex Home / users) | ✓ | ✓ | ~ |

Règle : **aucun écran ne disparaît**, seuls des contrôles/onglets/items de menu apparaissent ou non.
Tabs statiques concernés : Watchlist et Playlists de `main.xml:32-44` deviennent conditionnels à
`caps.watchlist` / `caps.playlists`.

---

## 5. Connexion / auth & évolution de la config

`AppServer` gagne un champ discriminant **`type`** (`plex` | `jellyfin` | `emby` | `stremio`), défaut
`plex` (rétro-compat des `config.json` existants via `NLOHMANN_..._WITH_DEFAULT`). `AppConfig`
construit le `Backend` actif depuis le `type` du serveur actif (factory `media::makeBackend(AppServer,
AppUser)` — équivalent de `remote::create`).

Les accesseurs globaux (`getUrl()`, `getToken()`, `getAccountToken()`, `config.hpp:187-190`) restent
mais **ne sont plus lus par l'UI** : ils alimentent la construction du backend, et le backend détient
son propre état de connexion. À terme, les sites d'appel ne référencent plus que `backend()`.

Flux d'ajout de serveur (`ServerAdd`) : un **sélecteur de type** en tête, puis le sous-flux adéquat :
- **Plex** : flux PIN existant (`server_add.cpp`, inchangé).
- **Jellyfin/Emby** : saisie URL → `GET /System/Info/Public` → **Quick Connect** (initiate → poll →
  authenticate, même UX que le PIN, pas de clavier) **ou** login user/mot de passe
  (`POST /Users/authenticatebyname`). C'est le flux *original* de switchfin (`PLEX_MIGRATION.md §1.2`).
- **Stremio** (phase 3) : login compte `api.strem.io` (`POST /api/login` → `authKey`).

---

## 6. Contrat de lecture (le risque principal)

Le player (`player_view.cpp`) délègue au backend (cf. cartographie « playback ») :
- **`resolvePlayback(item, options)`** couvre `playMedia`/`playDirect`/`playTranscode`
  (`player_view.cpp:191-362`). Retourne l'URL mpv + options + mode. Plex : decision universal + start
  HLS, ou `Part.key` direct. Jellyfin : `POST /Items/{id}/PlaybackInfo` → DirectPlay `stream?static=1`
  ou `TranscodingUrl` HLS. Stremio : URL directe `stream.url`, ou torrent via serveur local
  (desktop) — `caps.transcode=false`.
- **`reportProgress(id, state, posMs, durMs)`** toutes les 10 s (`player_view.cpp:89-95`). Plex :
  `/:/timeline`. Jellyfin : `/Sessions/Playing[/Progress|/Stopped]`. Stremio : `datastorePut`
  (compte) ou local.
- **`markWatched`** au seuil 90 % (`maybeScrobble`, `player_view.cpp:395-403`) — explicite, distinct
  de `reportProgress(stopped)`.
- Liste épisodes (`getAllEpisodes`), chapitres (`Item::chapters`), pistes (`Media/Part/Stream`).
- **Markers/skip-intro** : `Item::markers` est parsé mais **non consommé** aujourd'hui
  (`player_view.cpp:195` demande, jamais lu). `caps.skipIntro` réservé à un ajout ultérieur.
- **Incohérence à corriger en passant** : le codec transcode est codé en dur `h264`
  (`player_view.cpp:308`) alors que `MPVCore::VIDEO_CODEC` (h264/hevc/av1, `mpv_core.hpp:148`) existe et
  n'est jamais lu. `PlaybackOptions::videoCodec` doit le porter.

---

## 7. Spécificités par backend

### PlexBackend (refactor de l'existant, comportement identique)
Tout le code Plex actuel migre ici : headers `X-Plex-*` (`plex.hpp:34-48`), endpoints (`plex/types.hpp`),
parsing `MediaContainer`/`from_json`, auth PIN + resources + probing (`plex_auth.cpp`), watchlist
provider (`watchlist.hpp`), transcode universal. `caps` : `watchlist=true, favorites=false,
serverSort=true, transcode=true, multiProfile=true (Home)`.

### JellyfinBackend (= Emby — API quasi identique)
Traduit l'API Jellyfin → `media::*` (table d'endpoints inversée de `PLEX_MIGRATION.md §1.4`). Header
`Authorization: MediaBrowser Client=…,Device=…,DeviceId=…,Version=…,Token=…` (`config.cpp:713-724` de
switchfin) ; Emby : `X-Emby-Authorization`. Container `{Items, TotalRecordCount, StartIndex}`. Unités :
**ticks ÷ 10 000 = ms**. `caps` : `favorites=true, watchlist=false, serverSort=true, transcode=true,
skipIntro=true (MediaSegments), multiProfile=true`. Auth : URL + Quick Connect / login.

### StremioBackend (phase 3 — compte api.strem.io)
Fan-out sur les addons de la collection du compte ; Cinemeta = métadonnées par défaut (ids `tt…`).
Catalogues = hubs (`/catalog/{type}/{id}.json`, pagination par `skip`, pas de tri serveur). Détail
`/meta` ; saisons/épisodes dérivés de `meta.videos[]` regroupés **côté client**. Lecture `/stream` :
`stream.url` direct partout, `infoHash` via streaming server local **desktop only**. Progression =
`LibraryItem.state.timeOffset/flaggedWatched` poussé par `datastorePut`. `caps` : `serverSort=false,
collections=false, playlists=false, transcode=false, watchlist=true (library temp), globalSearch=false`.

---

## 8. Phases & disposition des fichiers

CMake (`GLOB_RECURSE app/src/*.cpp`, `CMakeLists.txt:86`) ramasse automatiquement les nouveaux fichiers
— **aucune édition CMake nécessaire**.

```
app/include/api/
  media/types.hpp        # modèle pivot neutre (ex-plex/types.hpp, sans from_json)
  media/capabilities.hpp # struct Capabilities
  backend.hpp            # interface media::Backend + Then/OnError + GridQuery/PlaybackOptions/…
  json_util.hpp          # jstr/jint/jnum/jbool/jtags (génériques)
  plex/                  # PlexBackend : headers X-Plex-*, endpoints, mappers, auth, watchlist, transcode
  jellyfin/              # JellyfinBackend (phase 2)
  stremio/               # StremioBackend (phase 3)
app/src/api/
  plex/*.cpp  jellyfin/*.cpp  stremio/*.cpp
```

**Phase 1 — Fondation** (cette branche, étape 1) :
1. Créer `media/types.hpp` neutre + `json_util.hpp` (rename `plex::`→`media::`, retrait des `from_json`).
2. Créer `backend.hpp` (interface + structs param + `Capabilities`).
3. `PlexBackend` : déplacer headers/endpoints/mappers/auth/watchlist/transcode ; implémenter l'interface.
4. `AppConfig` : champ `type`, factory `makeBackend`, `backend()` accessor ; construire le backend actif
   dans `checkLogin`/switch.
5. Migrer les ~25 sites d'appel : `plex::getJSON(conf.getUrl(), conf.getToken(), …)` →
   `AppConfig::backend().verbe(…)`.
6. i18n : généraliser le branding « Plex » (about.brief, setting.server.connect_to) ; garder `plex.*`
   pour le sous-flux PIN.
→ **Compile et se comporte à l'identique.** Aucun changement visible.

**Phase 2 — Jellyfin/Emby** (cette branche, étape 2) :
sélecteur de type dans `ServerAdd` + flux Quick Connect/login + `JellyfinBackend` + capacités +
neutralisation UI gated.

**Phase 3 — Stremio** (branche ultérieure) : compte api.strem.io + addons + lecture platform-aware.

---

## 9. Risques & points de vigilance

- **Séparation modèle/mapping** : ne pas laisser de `from_json` Plex sur le modèle neutre (sinon
  Jellyfin/Stremio héritent d'un parsing Plex). Mappers internes aux backends.
- **`ratingKey` opaque** : ne jamais supposer un format (Plex numérique, Jellyfin GUID, Stremio `tt…`).
- **Watchlist Plex = account-scoped** (token compte ≠ serveur, ids provider non résolvables côté
  serveur) : reste une capacité à part, pas un simple « favori ».
- **Quick Connect Jellyfin** : vérifier sa disponibilité serveur (peut être désactivé → fallback login).
- **Stremio** : API compte non documentée (enveloppe `{result,error}` à valider), torrents desktop-only.
- **Rétro-compat config** : `type` absent ⇒ `plex` ; les `config.json` v0.1.x doivent continuer à charger.
