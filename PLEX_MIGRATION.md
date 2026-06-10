# Switchfin → pleNx : plan de migration Jellyfin → Plex

> **État (2026-06-09) : phases 0 à 5 réalisées** — l'application compile (desktop macOS) et ne contient
> plus aucune référence à l'API Jellyfin (`app/include/api/` = `http.hpp` + `plex*`).
> Reste : **validation sur serveur réel** (aucun test contre un PMS n'a encore été fait), build Switch
> (devkitPro), phase 6 optionnelle (markers/BIF/watchlist/sessions), traductions des clés `plex.*`
> (12 langues en anglais), nouvelle icône.
>
> Dégradations v1 assumées (revisitables) : recherche sans colonne épisodes ni pagination ; fiche
> personne réduite (cast affiché, pas de navigation) ; sélecteur de version film sans effet (1re version
> accessible) ; logos texte (pas de clearLogo) ; filtre « vus » retiré (Plex ne propose que unwatched) ;
> téléchargements en qualité originale uniquement ; stats de transcodage serveur retirées du panneau.

> Document de référence produit le 2026-06-09 à partir d'une analyse exhaustive de :
> - **switchfin** (ce dépôt) : client Jellyfin C++ (borealis + mpv), cible Switch/PSV/PS4/desktop
> - **plezy** (`./plezy/`, non versionné) : client Flutter Plex+Jellyfin, utilisé comme spécification de
>   référence du protocole Plex (toutes les citations `plezy/...` pointent vers du code vérifié)
>
> Toutes les affirmations sont citées `fichier:ligne`. Les unités de temps sont systématiquement précisées.

---

## 1. Architecture de switchfin (état actuel)

### 1.1 Couche API

- `app/include/api/jellyfin.hpp` : 3 templates async `getJSON` / `postJSON` / `deleteJSON` qui font
  `HTTP::get/post(AppConfig::getUrl() + url, {getAuth(token)})` puis parsent via nlohmann vers le type
  demandé, et resynchronisent sur le thread UI (`brls::sync`). Wrapper de pagination `jellyfin::Result<T>`
  (`Items`, `TotalRecordCount`, `StartIndex`) — `jellyfin.hpp:73-88`.
- `app/include/api/jellyfin/system.hpp` : constantes d'endpoints système + structs (auth, sessions, admin).
- `app/include/api/jellyfin/media.hpp` : constantes d'endpoints médias + tous les modèles
  (`Item`, `Detail`, `Episode`, `Season`, `Source`, `Stream`, `UserDataResult`…), macros NLOHMANN.
- `app/include/api/jellyfin/device.hpp` : `DeviceProfile` envoyé au POST PlaybackInfo.
- Les URLs sont formatées **sur les sites d'appel** (fmt) dans ~20 fichiers de tabs/vues : la couche API
  est fine, la connaissance des endpoints est répartie dans l'UI.

### 1.2 Auth/config

- `AppConfig` (singleton, `config.json`) stocke `users[]` (`id`, `name`, `access_token`, `server_id`),
  `servers[]` (`name`, `id`, `urls[]`) — `app/include/utils/config.hpp`.
- En-tête d'auth Jellyfin (`config.cpp:713-724`) :
  `Authorization: MediaBrowser Client="…", Device="…", DeviceId="…", Version="…", Token="…"`.
- Flux : ajout serveur = `GET /System/Info/Public` (`server_add.cpp:43`) ; login = `POST
  /Users/authenticatebyname` ou Quick Connect (initiate → poll 2 s → authenticate) (`server_login.cpp`).
- DeviceId par plateforme (`config.cpp:151-225`) : SHA-256 du compte Nintendo sur Switch, etc.

### 1.3 Lecture

`player_view.cpp:182-377` : `POST /Items/{id}/PlaybackInfo` (avec DeviceProfile) →
- DirectPlay : `GET /Videos/{id}/stream?static=true&mediaSourceId&playSessionId&tag` (`:348-360`)
- Transcode : `TranscodingUrl` HLS renvoyée par le serveur (`:362-367`)
- Rapports : `POST /Sessions/Playing` (start), `/Progress` (toutes les 10 s + pause/resume),
  `/Stopped` (`:379-419`). **Unité : ticks, 1 s = 10 000 000 ticks** (`media.hpp:82`).
- Sous-titres externes : `sub-add {server}{DeliveryUrl}` dans mpv (`player_view.cpp:56-68`).
- Chapitres → marqueurs sur la barre de progression (`player_view.cpp:159`).

### 1.4 Inventaire complet des appels serveur (par zone)

| Zone | Fichier | Endpoints Jellyfin |
|---|---|---|
| Accueil | `tab/home_tab.cpp` | `/Users/{u}/Views`, `/Users/{u}/Items/Resume`, `/Shows/NextUp`, `/Users/{u}/Items/Latest`, `/LiveTv/Programs/Recommended` |
| Bibliothèques | `tab/media_folder.cpp`, `tab/media_collection.cpp` | `/Users/{u}/Views`, `/Users/{u}/Items?parentId&sortBy&filters…`, `/Genres`, `/Artists`, `/DisplayPreferences/usersettings` (GET+POST) |
| Film | `tab/media_movie.cpp` | `/Users/{u}/Items/{id}`, `/Items/{id}/Similar` |
| Série | `tab/media_series.cpp` | `/Users/{u}/Items/{id}`, `/Shows/{id}/Seasons`, `/Shows/{id}/Episodes`, `/Shows/NextUp?seriesId`, `/Items/{id}/Similar`, `/Users/{u}/Items/{id}/SpecialFeatures` |
| Suggestions | `tab/suggest_movie.cpp`, `tab/suggest_show.cpp` | `/Movies/Recommendations`, `/Users/{u}/Items/Latest`, Resume/NextUp scopés |
| Musique | `tab/music_album.cpp`, `tab/song_list.cpp`, `view/music_view.cpp` | `/Users/{u}/Items/{albumId}`, `/Users/{u}/Items?parentId`, `/Audio/{id}/stream?static=true` |
| Playlists | `tab/playlist.cpp` | `/Playlists/{id}/Items` |
| Recherche | `tab/search_tab.cpp`, `view/search_list.cpp` | `/Users/{u}/Items?searchTerm&includeItemTypes` |
| Personnes | `view/people_source.cpp` | `/Users/{u}/Items/{personId}`, `/Users/{u}/Items?personIds` |
| Live TV | `tab/live_tv.cpp` | `/LiveTv/Channels`, `/LiveTv/Programs/Recommended` |
| Menu contextuel | `view/context_menu.cpp` | POST/DELETE `/Users/{u}/PlayedItems/{id}`, `/Users/{u}/FavoriteItems/{id}` |
| Tableau de bord | `tab/dashboard.cpp` | `/Items/Counts`, `/System/Info`, `/System/ActivityLog/Entries`, `/Sessions`, `/System/Restart`, `/ScheduledTasks`, `/System/Info/Storage`, `/Devices`, `/Users` |
| Téléchargements | `utils/download.cpp` | `/Users/{u}/Items/{id}`, `/Items/{id}/Download`, `/Videos/{id}/stream` (transcodé), image Primary |
| Lecture | `activity/player_view.cpp` | `/Items/{id}/PlaybackInfo`, `/Videos/{id}/stream`, `/Sessions/Playing[/Progress|/Stopped]`, `/Shows/{id}/Episodes` (navigation) |
| WebSocket | `api/websocket.cpp`, `activity/main_activity.cpp:16` | `ws(s)://{server}/socket?api_key&deviceId` ; commandes Playstate/Play/GeneralCommand, keepalive 20 s, capabilities |
| Danmaku | `activity/player_view.cpp:424-432`, `config.cpp:552` | `/api/danmu/{id}/raw` (plugin), `/Plugins` |
| Images | `utils/image.cpp` + sites d'appel | `/Items/{id}/Images/{Primary,Thumb,Logo,Backdrop}?tag&maxWidth/fillWidth&format=Png|Webp` |
| Réglages | `tab/setting_tab.cpp:124` | `POST /Sessions/Logout` |

Hors serveur : analytics Google (`api/analytics.cpp`, measurement id de switchfin), auto-update GitHub
(`utils/version.cpp:117-176`, pointe `dragonflylee/switchfin`), onglet « remote » (WebDAV/UMS/local —
indépendant de Jellyfin, à conserver tel quel).

---

## 2. Spécification Plex (vérifiée dans plezy)

### 2.1 En-têtes X-Plex-* (remplacent `Authorization: MediaBrowser …`)

Envoyés sur **chaque** requête au serveur (`plezy/lib/models/plex/plex_config.dart:50-67`) :

```
X-Plex-Client-Identifier: <UUID v4 généré une fois et persisté>
X-Plex-Product: Switchlex
X-Plex-Version: <version app>
X-Plex-Platform: <plateforme>
X-Plex-Client-Profile-Name: Generic
X-Plex-Device: <nom d'appareil>          (optionnel)
X-Plex-Token: <token>                    (dès qu'on en a un)
Accept: application/json                 (sinon Plex renvoie du XML !)
Accept-Charset: utf-8
```

Pour les URLs consommées hors client HTTP (mpv, images), le token passe en **query param**
`?X-Plex-Token=` (`plezy/lib/utils/plex_url_helper.dart:18-22`).

### 2.2 Authentification : flux PIN (équivalent UX du Quick Connect existant)

Aucune auth par mot de passe dans plezy — flux PIN uniquement (`plex_auth_service.dart`) :

1. `POST https://plex.tv/api/v2/pins` (headers communs, corps vide)
   → `{id, code}` (`plex_auth_service.dart:129-135`).
   **Attention** : sans `strong=true` → PIN **faible à 4 caractères**, le seul accepté en saisie
   manuelle sur plex.tv/link (notre UX). `strong=true` (utilisé par plezy) génère un code long
   destiné uniquement au paramètre `code=` de `app.plex.tv/auth#?…` ouvert dans un navigateur.
2. Afficher le code + l'URL `https://app.plex.tv/auth#?clientID=<uuid>&code=<code>&context[device][product]=Switchlex`
   (sur Switch : afficher le code et dire d'aller sur **plex.tv/link** depuis un téléphone)
3. Poll `GET https://plex.tv/api/v2/pins/{id}` — backoff 1 s → 2 s → 4 s, plafond 5 s, timeout 2 min
   (`poll_with_backoff.dart:18-39`). 404/410 = PIN expiré. Quand `authToken` non nul → **token de compte**.
4. Validation token : `GET https://plex.tv/api/v2/user` (200 = valide, 401/403 = révoqué).

Base primaire `https://clients.plex.tv/api/v2`, repli `https://plex.tv/api/v2` (`plex_auth_service.dart:108-116`).
Déconnexion : purement locale (pas d'appel serveur dans plezy).

### 2.3 Découverte des serveurs et choix de connexion

1. `GET https://clients.plex.tv/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=1`
   avec `X-Plex-Token: <token de compte>` (`plex_auth_service.dart:187-191`).
2. Filtrer `provides == "server"`. Chaque serveur : `name`, `clientIdentifier` (machine id),
   **`accessToken` propre au serveur** (≠ token de compte), `connections[]`
   (`protocol`, `address`, `port`, `uri`, `local`, `relay`).
3. Priorité de test (`plex_auth_service.dart:580-643`) : https+local → https+remote → https+relay →
   http+local → http+remote → http+relay. Probe = `GET {base}/` avec token (timeout 1,5 s pour
   l'endpoint mémorisé, 2 s en course parallèle). Mémoriser l'URL gagnante.
4. Saisie manuelle d'URL possible en complément (le token serveur reste nécessaire).
5. `GET {server}/identity` : machine id + version, sans auth (`plex_client.dart:845-848`).
6. Pas de découverte GDM dans plezy (UDP 32412/32414) — non requis.

**Trois tokens** : compte plex.tv → par-serveur (`accessToken` de resources) → par-utilisateur Home
(`POST https://clients.plex.tv/api/v2/home/users/{uuid}/switch[?pin=…]` → `authToken`,
erreur 403 code 1041 = mauvais PIN) (`plex_auth_service.dart:245-268`, `plex_home_switch.dart:74-81`).
Liste des profils Home : `GET https://clients.plex.tv/api/v2/home/users`.

### 2.4 Enveloppe de réponse et pagination

Toutes les réponses : `{"MediaContainer": {…, "Metadata": […] | "Directory": […] | "Hub": […]}}`
(`plex_client.dart:698-703`). Pagination via **query params** `X-Plex-Container-Start` /
`X-Plex-Container-Size` ; total dans l'en-tête `X-Plex-Container-Total-Size` ou champ
`totalSize`/`size` (`plex_client.dart:817-831, 928-933`).

→ Le `jellyfin::Result<T>{Items,TotalRecordCount,StartIndex}` devient un `plex::Container<T>` dont le
`from_json` lit `MediaContainer.Metadata/Directory`, `totalSize`, `offset`.

### 2.5 Navigation : table de correspondance des endpoints

| Fonction (site d'appel switchfin) | Jellyfin | Plex (vérifié plezy) |
|---|---|---|
| Liste des bibliothèques | `GET /Users/{u}/Views` | `GET /library/sections` → `Directory[]` (`type`: movie/show/artist/photo) (`plex_client.dart:901-906`) |
| Reprendre la lecture | `/Users/{u}/Items/Resume` | `GET /hubs/continueWatching?count=N` ou repli `GET /hubs?identifier=home.continue,home.ondeck` (`plex_client.dart:1421-1463`) |
| À suivre (Next Up global) | `/Shows/NextUp` | hub `home.ondeck` (même appel que ci-dessus, fusion/dédup par `grandparentRatingKey`) |
| À suivre (par série) | `/Shows/NextUp?seriesId` | `GET /library/metadata/{showKey}?includeOnDeck=1` → `Metadata[0].OnDeck.Metadata` (`plex_client.dart:1035-1094`) |
| Ajouts récents (par bibliothèque) | `/Users/{u}/Items/Latest?parentId` | `GET /library/sections/{key}/recentlyAdded` ou hubs de section `GET /hubs/sections/{key}?count=N` (`plex_client.dart:1918-1951`) |
| Grille de bibliothèque (tri/filtres/pages) | `/Users/{u}/Items?parentId&sortBy&sortOrder&filters&limit&startIndex` | `GET /library/sections/{key}/all?type=<n>&sort=<champ>:<asc|desc>&unwatched=1&genre=…&X-Plex-Container-Start/Size` (`library_query_translator.dart:51-105`) |
| Types numériques | `includeItemTypes=Movie,Series…` | `type=` 1 movie, 2 show, 3 season, 4 episode, 8 artist, 9 album, 10 track (`plex_constants.dart:19-31`) |
| Tris | `sortBy=SortName,DateCreated…` | `sort=titleSort`, `addedAt:desc`, `originallyAvailableAt`, `rating`, `viewCount`, `userRating` ; liste dynamique via `GET /library/sections/{key}/sorts` (`plex_client.dart:1816-1914`) |
| Genres | `GET /Genres?parentId` | `GET /library/sections/{key}/genre` (Directory id+titre) puis filtre `…/all?genre={id}` |
| Détail d'un item | `GET /Users/{u}/Items/{id}` | `GET /library/metadata/{ratingKey}?includeChapters=1&includeMarkers=1&includeStreams=1&checkFiles=1` (`plex_client.dart:1607-1626`) |
| Similaires | `GET /Items/{id}/Similar` | `GET /hubs/metadata/{ratingKey}/related?count=10` (`plex_client.dart:1981-2006`) |
| Bonus/Special features | `/Users/{u}/Items/{id}/SpecialFeatures` | `GET /library/metadata/{ratingKey}/extras` (`plex_client.dart:1512-1522`) |
| Saisons d'une série | `GET /Shows/{id}/Seasons` | `GET /library/metadata/{showKey}/children` (`plex_client.dart:1470-1480`) |
| Épisodes d'une saison | `GET /Shows/{id}/Episodes?seasonId` | `GET /library/metadata/{seasonKey}/children` ; tous les épisodes d'une série : `…/{showKey}/grandchildren` (`plex_client.dart:1485-1508`) |
| Recherche | `/Users/{u}/Items?searchTerm` | `GET /library/search?query=…&limit=…&searchTypes=movies,tv&includeCollections=1` → `MediaContainer.SearchResult[].Metadata` (`plex_client.dart:1367-1378`) |
| Recommandations films | `/Movies/Recommendations` | pas d'équivalent direct → hubs de section `GET /hubs/sections/{key}` |
| Artistes | `GET /Artists?parentId` | `…/all?type=8` ; albums d'un artiste / pistes d'un album : `…/children` |
| Playlists (items) | `GET /Playlists/{id}/Items` | `GET /playlists?playlistType=video|audio` ; `GET /playlists/{id}/items` (pagination idem) (`plex_client.dart:2069-2122`) |
| Collections (BoxSet) | type `BoxSet` via `/Users/{u}/Items` | `GET /library/sections/{key}/collections` ; enfants : `GET /library/collections/{id}/children` (`plex_client.dart:2444-2491`) |
| Personnes (fiche + filmographie) | `/Users/{u}/Items/{personId}`, `?personIds` | pas de fiche personne ; filtrage `…/all?actor={tagId}` ou hubs related — **à dégrader** (cf. §4) |
| Marquer vu / non-vu | POST/DELETE `/Users/{u}/PlayedItems/{id}` | `GET /:/scrobble?key={ratingKey}&identifier=com.plexapp.plugins.library` / `GET /:/unscrobble?…` (`plex_client.dart:1681-1700`) |
| Favori | POST/DELETE `/Users/{u}/FavoriteItems/{id}` | **inexistant chez Plex** (cf. décision D3) ; note : `PUT /:/rate?key=…&identifier=…&rating=0-10` (−1 efface) |
| Retirer de « Reprendre » | — | `PUT /actions/removeFromContinueWatching?ratingKey=…` (bonus possible) |
| Images | `/Items/{id}/Images/Primary?tag&maxWidth` | `{base}{thumb|art}?X-Plex-Token=…` ; redimensionné : `GET /photo/:/transcode?width&height&minSize=1&upscale=1&url=<chemin encodé avec token>&X-Plex-Token=…` (`plex_client.dart:4019-4056`) |
| Avatar utilisateur | `/Users/{u}/Images/Primary` | `thumb` du compte plex.tv (réponse `/api/v2/user` / home users) |
| Téléchargement fichier | `GET /Items/{id}/Download?api_key` | `{base}{Part.key}?download=1&X-Plex-Token=…` (qualité originale uniquement) |
| Préférences d'affichage serveur | `/DisplayPreferences/usersettings` | inexistant → stocker dans `config.json` local |
| Logout | `POST /Sessions/Logout` | local uniquement (suppression token) |

Champs `Metadata` les plus utiles (`plex_mappers.dart:551-724`) : `ratingKey`, `key`, `type`, `title`,
`summary`, `year`, `thumb`, `art`, `duration` (**ms**), `viewOffset` (**ms**), `viewCount`,
`leafCount`/`viewedLeafCount`/`childCount`, `index`/`parentIndex`,
`parentRatingKey`/`grandparentRatingKey` (+ `parentTitle`/`grandparentTitle`/`grandparentThumb`),
`addedAt`/`updatedAt`/`lastViewedAt` (**epoch s**), `contentRating`, `rating`/`audienceRating` (0-10),
`Genre[].tag`, `Role[]` (`tag`=nom, `role`, `thumb`), `Media[]→Part[]→Stream[]`
(`streamType` 1=vidéo 2=audio 3=sous-titre ; sous-titre externe : champ `key`).

### 2.6 Conversion des unités (piège principal)

| Grandeur | Jellyfin (switchfin) | Plex |
|---|---|---|
| Durée / position | ticks (1 s = 10 000 000) `media.hpp:82` | **millisecondes** (`duration`, `viewOffset`, `time`, chapitres, markers) |
| Offset de seek transcode | ticks | **secondes entières** (`offset`) (`plex_client.dart:3193`) |
| Horodatages | ISO-8601 | **epoch secondes** (`addedAt`…) |
| Progression « vu » | `UserData.Played`, `PlayedPercentage` | `viewCount > 0` ; seuil vu = pref serveur `LibraryVideoPlayedThreshold` (défaut 90 %) |

### 2.7 Lecture

**Lecture directe** (`plex_playback_mapper.dart:59-130`) :
`GET /library/metadata/{key}?includeChapters=1&includeMarkers=1&includeStreams=1&checkFiles=1`
→ choisir `Media[i]`/`Part` accessible → URL mpv = `{base}{Part.key}?X-Plex-Token=…`.
Position de reprise : option mpv `start=<secondes>` (comme aujourd'hui).

**Transcodage** (`plex_client.dart:3026-3196`) — approche plezy : **MKV sur HTTP, pas HLS**
(HLS posait problème avec les sous-titres dans mpv, commentaire `plex_client.dart:3145`) :

1. `GET /video/:/transcode/universal/decision?<params>` — codes : ≥2000 = échec,
   `transcodeDecisionCode` 1000 = direct play seulement, 1001 = transcode OK (`:3244-3278`).
2. URL mpv = `{base}/video/:/transcode/universal/start?<mêmes params>&X-Plex-Token=…`

Params essentiels : `hasMDE=1`, `path=/library/metadata/{key}`, `mediaIndex`, `partIndex=0`,
`protocol=http`, `fastSeek=1`, `directPlay=0|1`, `directStream=0|1`, `directStreamAudio=0`,
`maxVideoBitrate=<kbps>`, `subtitles=embedded|none`, `subtitleStreamID`, `advancedSubtitles=text`,
`audioStreamID`, `copyts=1`, `mediaBufferSize=102400`, `session=<id aléatoire 24 char>`,
`offset=<s>` (seek), `X-Plex-Session-Identifier=<id stable>`, `X-Plex-Platform=Generic`
(**obligatoire** : `Flutter`/noms exotiques → HTTP 400), et
`X-Plex-Client-Profile-Extra=add-transcode-target(type=videoProfile&context=streaming&protocol=http&container=mkv&videoCodec=h264%2Chevc%2C*&audioCodec=opus%2Cvorbis%2Cflac%2C*&subtitleCodec=ass%2Cpgs%2Cvobsub%2C*)+add-settings(DirectPlayStreamSelection=true)`
(+ `add-limitation(scope=videoCodec&scopeName=*&type=upperBound&name=video.bitrate&value=<kbps>&replace=true)`)
— parenthèses percent-encodées (`%28 %29`) (`plex_client.dart:3125-3143, 3235-3242`).

Seek pendant transcode : régénérer `session`, relancer `start` avec `offset=<s>`, et décaler la timeline
affichée (plezy : `timelineOffset`) (`plezy/lib/screens/video_player/parts/seeking.dart`).
Le profil DeviceProfile Jellyfin (`device.hpp`) disparaît au profit des params ci-dessus
(adapter `videoCodec=` selon les capacités : Switch = h264,hevc[,av1 sur modèles récents] —
reprendre la logique existante de `player_view.cpp:182-300`).

**Pistes audio/sous-titres** :
- Direct play : sélection locale mpv (comme aujourd'hui) ; sous-titres externes :
  `sub-add {base}{Stream.key}.{ext}?encoding=utf-8&X-Plex-Token=…` (`plex_client.dart:3521-3522`).
- Transcode : `audioStreamID`/`subtitleStreamID` dans les params (relance du start) ; sous-titres texte
  intégrés au MKV via `subtitles=embedded`, PGS/externes non intégrables.
- Persistance serveur (optionnel) : `PUT /library/parts/{partId}?audioStreamID=…&subtitleStreamID=…&allParts=1`.

**Rapports de lecture** → remplace `/Sessions/Playing*` (`plex_client.dart:1703-1727`) :

```
POST /:/timeline?ratingKey=<key>&key=/library/metadata/<key>&state=playing|paused|stopped
                &time=<ms>&duration=<ms>
```
Cadence identique à l'existant (10 s ; ~60 s en pause). À la fin : **scrobble explicite** quand
`position/durée ≥ seuil` (pref serveur, défaut 90 %) — `state=stopped` ne suffit PAS à marquer vu
(`plex_client.dart:4061-4065`, `playback_progress_tracker.dart:321-345`).

**Chapitres** : `Chapter[]` (`startTimeOffset`/`endTimeOffset` en ms) — remplace `Item.Chapters` (ticks).
**Markers** (nouveau vs Jellyfin) : `Marker[]` `type=intro|credits` → bouton « passer l'intro » possible.
**Trickplay** : BIF via `GET /library/parts/{partId}/indexes/sd` (format binaire documenté
`bif_thumbnail_service.dart:25-64`) — amélioration optionnelle.
**Musique** : lecture directe identique (`{base}{Part.key}?X-Plex-Token=`), pistes via `type=10`.

### 2.8 Temps réel / WebSocket

Jellyfin `ws://…/socket` (remote control + keepalive) n'a pas d'équivalent 1:1.
Plex expose `ws(s)://{server}/:/websockets/notifications?X-Plex-Token=…` (événements serveur :
timeline, activités) ; le pilotage à distance (« lire sur cet appareil ») passe par **Plex Companion**
(protocole distinct, non implémenté dans plezy). → cf. décision D2.

---

## 3. Stratégie de transformation

### 3.1 Approche retenue pour la couche API (cf. décision D1)

Créer `app/include/api/plex.hpp` + `app/include/api/plex/` sur le modèle de l'existant :

- `plex.hpp` : `getContainer<T>` / `getJSON<T>` / `postJSON` / `request` avec en-têtes X-Plex-*,
  `Accept: application/json`, et un `plex::Container<T>` (from_json lisant `MediaContainer`) exposant
  la même forme de pagination que `jellyfin::Result<T>`.
- `plex/auth.hpp` : flux PIN + resources + course de connexions (remplace server_add/server_login).
- `plex/media.hpp` : modèles (`Item` avec `ratingKey/title/type/thumb/viewOffset…`), constantes
  d'endpoints, types numériques, helpers d'URL d'images.
- `plex/playback.hpp` : décision/params transcode, timeline, scrobble.

`AppConfig` : `AppServer{name, clientIdentifier, accessToken, connections[], preferredUri}` ;
`AppUser{uuid, name, thumb, authToken (compte ou Home), homePin?}` ; `getAuth()` → en-têtes X-Plex-*.

### 3.2 Phases

| Phase | Contenu | Fichiers principaux |
|---|---|---|
| **0 — Nettoyage/branding** | Renommer Switchfin→Switchlex (`CMakeLists.txt:54-64` : projet, `PACKAGE_NAME`, **nouveau title id Switch**, icônes), retirer analytics GA (`api/analytics.*`), retirer danmaku si D2 confirmé (`view/danmaku_*`, `config.cpp:552`, i18n), repointer l'auto-update sur `thcolin/switchlex` (`config.hpp:20`, `version.cpp:148`), README, `.gitignore` += `plezy/` | CMakeLists, main.cpp, resources/, utils/version.cpp |
| **1 — Fondation API Plex** | `plex.hpp` + modèles + auth PIN + resources + course de connexions + refonte `AppConfig` | app/include/api/plex*, utils/config.* |
| **2 — UI connexion** | `server_add` (PIN + URL manuelle), `server_login` (sélection serveur/profil Home), `server_list` | tab/server_*, activity/server_list.cpp |
| **3 — Navigation** | home (hubs), bibliothèques (sections/all + tris/filtres), détail film/série/saisons/épisodes, recherche, genres, similaires, menu contextuel (scrobble), images | tab/*, view/video_card, view/context_menu, utils/image |
| **4 — Lecture** | direct play → décision/transcode MKV-HTTP → timeline/scrobble → pistes/sous-titres → chapitres ; navigation épisode suivant | activity/player_view.cpp, view/player_setting.cpp, view/video_view.cpp |
| **5 — Compléments** | collections, playlists, musique (direct play), téléchargements (original), photos | tab/playlist, tab/music_*, utils/download |
| **6 — Optionnel** | markers (skip intro), BIF/trickplay, websocket notifications, sessions actives (`/status/sessions`), watchlist plex.tv, Live TV Plex | — |

Chaque phase doit compiler et être testable seule. La phase 4 est le risque principal
(transcode universel : profil `Generic` + `add-transcode-target`, codes de décision, seek-offset).

---

## 4. Décisions — TRANCHÉES le 2026-06-09

| # | Sujet | Décision |
|---|---|---|
| **D1** | Forme des modèles internes | **Natifs Plex** (`ratingKey`, `title`, durées en ms) — adaptation des ~20 fichiers UI |
| **D2** | Danmaku, dashboard admin, Live TV, remote control WebSocket | **Supprimés en v1** (réintroduction éventuelle phase 6). **Exigence ajoutée : porter le téléchargement + lecture locale vers Plex** (la feature existe dans switchfin — `utils/download.cpp`, `tab/download_tab.cpp`, client `local` — à préserver pendant les suppressions et porter en phase 5) |
| **D3** | « Favoris » | **Supprimé en v1** (watchlist plex.tv envisageable en phase 6) |
| **D4** | Musique (albums/pistes/lecteur audio/playlists audio) | **Supprimée** (tabs musique, music_view, song_list ; les playlists vidéo Plex restent possibles en phase 5/6) |

Décisions techniques actées (documentées ci-dessus, contester si désaccord) :
auth **PIN + saisie manuelle URL+token** en secours ; préférences de tri **locales** ;
suppression de l'analytics GA.

**Révision (phase 4)** : transcode en **HLS** (`protocol=hls`, conteneur mpegts) et non MKV/HTTP.
Raison : le seek de switchfin appelle `mpv.seek()` directement (slider/boutons) et reposait déjà sur
le HLS Jellyfin, nativement seekable dans mpv ; le MKV/HTTP de plezy impose une relance du transcodeur
à chaque seek hors tampon (`seeking.dart`), mécanique étrangère au lecteur actuel. Les sous-titres
sélectionnés pendant un transcode sont **incrustés** (`subtitles=burn`), les sous-titres externes
restent chargés en sidecar mpv. Le MKV/HTTP reste documenté §2.7 si l'incrustation devait être évitée.

Note téléchargements Plex (D2) : qualité **originale** fiable via `{base}{Part.key}?download=1&X-Plex-Token=` ;
le téléchargement transcodé (équivalent des presets 1080p/720p/480p actuels de `download.cpp:213`) devra être
investigué en phase 5 (le transcodeur universel peut throttler sans rapports timeline ; l'API Downloads
officielle exige un Plex Pass). La lecture locale des fichiers téléchargés ne dépend pas du serveur et
fonctionne déjà (`client/local.cpp`).
