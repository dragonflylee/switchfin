# SPEC — Navigation hors-ligne des téléchargements (issue #19)

> Fork pleNx (client Plex natif, gamepad-first, C++/Borealis — Switch, Vita, PS4,
> desktop). Cette spec couvre la fonctionnalité « Offline Download Browsing »
> (GitHub thcolin/pleNx#19) et sert de contrat avant implémentation.
>
> Statut : **à valider** avant tout code.

---

## 1. Objectif

Aujourd'hui, les contenus téléchargés ne sont consultables **que** sous forme d'une
**liste plate** (`DownloadView`, sections « En cours » / « Téléchargés » —
`app/src/tab/download_tab.cpp`, `DownloadDataSource::addSection`), et **uniquement
quand le serveur est joignable** (serveur injoignable → `ServerList`, jamais les
téléchargements — `app/src/main.cpp:168-181`).

L'issue #19 demande de **naviguer les téléchargements comme en ligne** : structure
en dossiers bibliothèques → films / séries → saisons → épisodes, en ne montrant que
les médias effectivement présents localement.

**Cible :** l'utilisateur nomade (train, avion, Switch/Vita sans réseau) qui a
téléchargé des films/épisodes et veut retrouver l'expérience de navigation complète
(fiches, posters, casting, saisons) **sans réseau**.

### Décisions produit (validées par l'utilisateur)

| Axe | Décision retenue |
|---|---|
| **Portée** | **Mode hors-ligne global** : quand le serveur est injoignable, Accueil et bibliothèques n'affichent que les contenus téléchargés, navigables comme en ligne. |
| **Richesse des fiches** | **Fiche complète + casting** : cache poster, backdrop, logo, textes (résumé, genres, notes, durée) ET photos du casting. Rangées « similaires » / filmographie masquées hors-ligne. |
| **En ligne** | **Toujours fiche locale** depuis l'espace Téléchargements : un contenu téléchargé se rend depuis le cache local, même connecté (instantané, cohérent). Le réseau ne sert qu'à découvrir/ouvrir le contenu **non** téléchargé. |
| **Séries partielles** | **Structure complète, épisodes manquants grisés** : la saison affiche tous ses épisodes ; ceux non téléchargés sont grisés et non lisibles. |

### Non-objectifs (v1)

- Pas de rangées « similaires » / filmographie hors-ligne (contenu non téléchargé).
- Pas de watchlist / playlists hors-ligne (nécessitent le compte plex.tv / le serveur).
- Pas de synchronisation des positions de lecture (`viewOffset`) vers le serveur
  hors-ligne (voir garde-fou « ne jamais deviner une valeur serveur »). Reprise
  **locale** possible en option (stretch).
- Pas de nouvelle dépendance de stockage (pas de SQLite) : on reste sur du JSON
  `nlohmann` comme le reste de l'app.

---

## 2. Contexte technique vérifié (ancrages)

| Élément | Fichier / ancre |
|---|---|
| Modèle média unique `plex::Item` (movie/show/season/episode) | `app/include/api/plex/types.hpp:388` (uniquement `from_json`, **pas** de `to_json`) |
| Hiérarchie | `parentRatingKey`/`parentIndex` (saison), `grandparentRatingKey` (série), `index`, `leafCount` — `types.hpp:405-426` |
| Gestionnaire de téléchargements (singleton) | `app/include/utils/download.hpp`, `app/src/utils/download.cpp` |
| `DownloadItem` (snapshot pauvre) | `download.hpp:22-41` |
| Capture métadonnée au download | `download.cpp:51-111` (`GET /library/metadata/{ratingKey}`) |
| Assets téléchargés | vidéo + **une** `thumb.png` + `metadata.json` — `download.cpp:324-439` |
| Layout disque | `{configDir}/downloads/{index.json, {ratingKey}/video.ext, thumb.png, metadata.json}` — `download.cpp:9` |
| Liste plate | `DownloadDataSource` — `download_tab.cpp:189-313` |
| Lecture fichier local | `RemoteView::play(path, detail, "Local")` — `download_tab.cpp:255`, `remote_view.cpp:475` |
| Fiche film | `app/src/tab/media_movie.cpp`, `resources/xml/tabs/movie.xml` |
| Fiche série / saison | `app/src/tab/media_series.cpp` (`MediaSeries`, `MediaSeason`, `SeasonDataSource`), `series.xml`/`seasons.xml` |
| Dispatch par type | `app/src/view/video_source.cpp:105-134` |
| Empilement des fiches | `ui::presentDetail` — `app/src/view/auto_tab_frame.cpp:527` |
| Bibliothèques → onglets | `app/src/activity/main_activity.cpp:11-57` (`GET /library/sections`) |
| Chargement image (pas de cache disque) | `app/include/utils/image.hpp:19`, `app/src/utils/image.cpp` (cache texture GPU volatil uniquement) |
| Porte hors-ligne au démarrage | `main.cpp:160-183`, `AppConfig::checkLogin()` `config.cpp:518-542` |
| Persistance | JSON `nlohmann` : `config.json` + `downloads/index.json` ; **pas de SQLite** |
| i18n | `resources/i18n/<lang>/main.json` — **14 langues** à maintenir en parité |

**Contrainte forte :** il n'existe **aucune abstraction backend** (`media::Backend`
n'existe pas sur cette branche) — l'app est câblée en dur sur Plex via des fonctions
libres `plex::`. La branche multi-backend est séparée. On n'étend donc pas une
interface existante : on introduit un **seam** minimal (§4.3).

---

## 3. Exigences fonctionnelles & critères d'acceptation

Chaque critère doit être **vérifiable** (harnais de captures + simulation hors-ligne, §8).

### 3.1 Capture enrichie au téléchargement
- **AC1** — Télécharger un **film** persiste, en plus du fichier vidéo : la fiche
  complète (résumé, année, durée, notes critique/audience, genres, casting) + les
  assets poster, backdrop (`art`), logo (`clearLogo`) et vignettes du casting.
- **AC2** — Télécharger un **épisode** persiste aussi la fiche de sa **saison** et de
  sa **série** (résumé, art, logo, poster, casting série), + la **liste complète**
  des épisodes de la saison (métadonnées seules pour les non téléchargés) et la
  **liste complète des saisons** de la série.
- **AC3** — Télécharger une **série entière** (`doDownloadSeries`, `allLeaves`)
  produit le même catalogue cohérent sans requêtes redondantes.
- **AC4** — Toute capture est **best-effort** : l'échec d'un asset secondaire (une
  vignette de casting) n'empêche pas le téléchargement de réussir.

### 3.2 Rendu des fiches depuis le cache local
- **AC5** — Ouvrir un film/série/saison **téléchargé** rend une fiche visuellement
  équivalente à la fiche serveur (poster, backdrop, logo, textes, notes, casting),
  **entièrement hors réseau**.
- **AC6** — Depuis l'espace Téléchargements, la fiche locale est utilisée **même
  connecté** (décision « toujours fiche locale »).
- **AC7** — Hors-ligne, les rangées « similaires » et la navigation vers la
  filmographie d'un acteur sont **masquées** (pas d'état d'erreur).
- **AC8** — Le bouton « Télécharger » d'une fiche locale reflète l'état
  (Téléchargé / supprimer) ; « Watchlist » masqué hors-ligne.

### 3.3 Séries partielles
- **AC9** — La vue saison affiche **tous** les épisodes de la saison ; les non
  téléchargés sont **grisés** et **non sélectionnables pour lecture**.
- **AC10** — La rangée « Saisons » d'une série n'affiche que les saisons ayant ≥ 1
  épisode téléchargé ; le compteur d'épisodes reflète la structure serveur (leafCount).
- **AC11** — Lancer un épisode téléchargé lit le fichier local ; un épisode grisé
  propose (en ligne) de le télécharger, ou (hors-ligne) est inerte / info « non
  disponible hors-ligne ».

### 3.4 Mode hors-ligne global
- **AC12** — Au démarrage, si `checkLogin()` échoue **et** que le catalogue local
  n'est pas vide → l'app entre en **mode hors-ligne** (au lieu de `ServerList`) :
  sidebar reconstruite depuis les bibliothèques du catalogue local.
- **AC13** — En mode hors-ligne : Accueil montre un regroupement des téléchargements ;
  chaque onglet bibliothèque montre la grille des contenus téléchargés de cette
  section ; la recherche interroge le catalogue local ; Watchlist/Playlists sont
  masqués ou en état vide « indisponible hors-ligne ».
- **AC14** — Un moyen explicite de **repasser en ligne / réessayer** reste accessible
  (retour vers `ServerList` / re-probe serveur).
- **AC15** — Remote/UMS (WebDAV/FTP/USB, LAN/local) restent fonctionnels hors-ligne
  (inchangés). Settings reste fonctionnel.
- **AC16** — Si le catalogue local est vide et le serveur injoignable → comportement
  actuel conservé (`ServerList`), avec un message clair.

### 3.5 Cache d'images hors-ligne
- **AC17** — `Image::load` résout depuis le cache disque local quand on est
  hors-ligne (ou pour un asset enregistré comme local), sinon conserve le chemin
  réseau actuel. Aucun scintillement / placeholder cassé sur une fiche locale.

### 3.6 Cycle de vie / stockage
- **AC18** — Supprimer un téléchargement (carte, X/Y, cascade) purge fichier vidéo
  **et** assets/metadonnées orphelins ; supprimer le dernier épisode d'une saison
  purge les nœuds saison/série devenus vides du catalogue.
- **AC19** — L'en-tête « Stockage » (`updateStorage`) comptabilise correctement les
  nouveaux assets (art/logo/casting) dans les octets pleNx.
- **AC20** — **Compatibilité ascendante** : un `downloads/index.json` préexistant se
  charge sans casse ; un catalogue est reconstruit au mieux (fiche minimale pour
  l'ancien contenu, fiche complète pour les nouveaux téléchargements).

---

## 4. Conception

Principe directeur (CLAUDE.md) : **réutiliser le modèle et les vues existants**,
introduire le **strict minimum** d'indirection, **aucun deuxième modèle média**.

### 4.1 Couche de données locale (persistance des fiches + assets)

**Décision : persister des snapshots `plex::Item` complets**, exactement le struct que
les vues consomment déjà, afin que `MediaMovie` / `MediaSeries` / `MediaSeason` /
grilles / cartes se rendent **sans modification de rendu** — seule leur *source de
données* change.

Cela impose :
- Ajouter un **`to_json`** pour `plex::Item` et les structs imbriqués nécessaires
  (`Media`, `Part`, `Stream`, `Role`, `Chapter`, `Marker`, `Section`) — ou un helper
  de sérialisation dédié — pour round-tripper un Item vers/depuis le disque.
- Ajouter à `plex::Item` les champs de rattachement bibliothèque
  (`librarySectionID`, `librarySectionTitle`) — **à vérifier** dans une réponse Plex
  réelle avant de l'affirmer.
- Enrichir `DownloadItem` avec `parentRatingKey`, `grandparentRatingKey`,
  `librarySectionID` (permet migration/rebuild), OU faire du catalogue la source de
  vérité et réduire `DownloadItem` au fichier + statut + progression.

**Layout disque cible** (additif, compat ascendante) :

```
{configDir}/downloads/
  index.json                # DownloadItem[] : fichiers réels + statut/progression (rôle conservé)
  catalog.json              # OfflineCatalog : sections[] + registre de nœuds (ratingKey → type, parent, hasFile, childrenKeys[])
  meta/{ratingKey}.json     # snapshot plex::Item complet (la « fiche ») : movie/show/season/episode
  media/{ratingKey}/video.{ext}   # UNIQUEMENT les feuilles téléchargées (layout {ratingKey}/ actuel conservable)
  art/index.json            # map cheminImagePlex → art/{clé}.img
  art/{clé}.img             # image d'origine mise en cache (poster/art/logo/casting)
```

**Contenu du catalogue** (le « garder en mémoire » demandé) :
- Toute **feuille** téléchargée (film ou épisode) : Item complet **avec** `Media/Part/
  Stream` (nécessaire à la lecture locale + choix version/sous-titres hors-ligne).
- Tout **ancêtre** d'un épisode : Item **saison** + Item **série** (champs fiche complets).
- Liste **complète** des épisodes de chaque saison ayant ≥ 1 feuille téléchargée
  (métadonnées seules pour les non téléchargés → grisés, AC9).
- Liste **complète** des saisons de la série (rangée « Saisons », AC10).
- La **section** de rattachement (reconstruction de la sidebar hors-ligne, AC12).

**Extension du pipeline de download** (`DownloadManager::doDownload` + une nouvelle
étape « capture fiche ») :
- Film : `GET /library/metadata/{id}` (déjà fait, capturer TOUT) → écrire `meta/`,
  télécharger art/logo/poster + vignettes casting.
- Épisode : épisode + `GET .../{seasonId}`, `.../{showId}`, `.../{seasonId}/children`
  (tous les épisodes), `.../{showId}/children` (toutes les saisons) + assets + casting série.
- Série entière : réutiliser `allLeaves` (`media_series.cpp`), dédupliquer les fetch
  d'ancêtres/assets.

### 4.2 Cache d'images hors-ligne

- Nouveau cache disque `downloads/art/`, clé = hash stable du **chemin image Plex**
  (`thumb`, `art`, `clearLogo`, `Role.thumb`, `parentThumb`, `grandparentThumb`,
  `grandparentArt`). On met en cache **l'image d'origine** une fois par chemin ;
  `Image::load` avec une taille cible charge l'original local (mise à l'échelle GPU
  côté Borealis) — hors-ligne on **contourne** `/photo/:/transcode`.
- `Image::load` (`image.hpp:19`) gagne une branche : si hors-ligne (ou si le chemin
  est enregistré dans `art/index.json`) → `setImageFromFile(local)` ; sinon chemin
  réseau actuel inchangé.
- Cas des vignettes casting en URL absolue (tmdb…) : mettre en cache l'URL utilisée
  par la vue, telle quelle.

### 4.3 Seam de source média + routage

**Décision : introduire un seam minimal `MediaSource`** (le « correct fix », pas
l'interception fragile dans `plex::getJSON`).

- Interface fine couvrant **exactement** les opérations que les vues de navigation /
  fiches utilisent : `sections()`, `sectionItems()`, `metadata()`, `children()`,
  `allLeaves()`, `related()` (vide hors-ligne). Signatures calquées sur le style
  callback existant (`plex::getJSON(onOk, onErr, ...)`).
- Deux implémentations :
  - `PlexSource` — enveloppe les fonctions libres `plex::` actuelles (comportement
    identique en ligne).
  - `OfflineSource` — lit `catalog.json` / `meta/` / `art/`.
- Résolution `MediaSource::current(context)` :
  - **Global hors-ligne** (serveur injoignable) → `OfflineSource` partout.
  - **En ligne, contexte Téléchargements OU item téléchargé** → `OfflineSource`
    (satisfait « toujours fiche locale »).
  - **Sinon** → `PlexSource`.

**Rejeté :** interception par table de dispatch endpoint→local dans `plex::getJSON`
(templété, piloté par chaîne d'endpoint) → contrôle de flux caché et fragile. C'est le
raccourci, pas la solution correcte.

Migration mécanique : dans `media_movie.cpp`, `media_series.cpp`, `media_collection.cpp`,
`home_tab.cpp`, `main_activity.cpp`, `video_source.cpp`, remplacer les appels directs
`plex::getJSON(...)` des surfaces de navigation par `MediaSource::current(...)->...`.
La lecture d'un fichier local existe déjà (`RemoteView::play`) — inchangée.

### 4.4 Détection & entrée hors-ligne

- État réseau : un flag/`NetworkState` (runtime). Positionné hors-ligne quand
  `checkLogin()` échoue au démarrage (`main.cpp`) **et** catalogue non vide → pousser
  une **MainActivity en mode hors-ligne** au lieu de `ServerList`.
- Sidebar hors-ligne (`MainActivity::addLibraryTabs`) : sections issues du catalogue
  local. Accueil : hub(s) « Téléchargés ». Recherche : catalogue local. Watchlist /
  Playlists : masqués ou état vide « indisponible hors-ligne ». Remote/UMS/Settings :
  inchangés.
- Affordance explicite « Repasser en ligne / réessayer » (AC14).
- Bascule automatique en cours de session sur échec réseau : **hors périmètre v1**
  (déclencheur principal = probe démarrage + bascule explicite). La lecture gère déjà
  un dialogue « injoignable » (`player_view.cpp:229`).

### 4.5 Découpage en phases

1. **Modèle + capture riche** : `to_json`, champs section, layout `meta/`+`catalog.json`,
   extension pipeline download (ancêtres, children complets, assets), migration.
2. **Cache image disque** + branche offline de `Image::load`.
3. **Seam `MediaSource`** + rendu local des fiches/grilles (marche aussi en ligne, AC6).
4. **Mode hors-ligne global** : détection démarrage, MainActivity offline, sidebar/
   accueil/recherche, règles par surface, épisodes grisés (AC9), toggle online.
5. **Finition** : états vides, i18n 14 langues, comptabilité stockage, purge en cascade.

---

## 5. Structure du projet (fichiers)

**Nouveaux**
- `app/include/utils/offline_library.hpp` + `app/src/utils/offline_library.cpp` —
  `OfflineCatalog` / persistance `catalog.json` + `meta/` + purge en cascade.
- `app/include/media/media_source.hpp` (+ `.cpp`) — seam `MediaSource`, `PlexSource`,
  `OfflineSource`.
- (option) `app/include/utils/image_cache.hpp` — cache disque d'assets si non intégré
  directement à `image.*`.

**Modifiés**
- `app/include/api/plex/types.hpp` — `to_json` (Item + imbriqués), champs
  `librarySectionID`/`librarySectionTitle`.
- `app/include/utils/download.hpp` / `app/src/utils/download.cpp` — champs enrichis,
  étape « capture fiche + ancêtres + assets », purge en cascade.
- `app/src/utils/image.cpp` / `app/include/utils/image.hpp` — branche offline.
- `app/src/tab/media_movie.cpp`, `app/src/tab/media_series.cpp`,
  `app/src/tab/media_collection.cpp`, `app/src/tab/home_tab.cpp`,
  `app/src/tab/search_tab.cpp` — passage par `MediaSource`.
- `app/src/view/video_source.cpp` — dispatch/état grisé des épisodes.
- `app/src/activity/main_activity.cpp` — sidebar offline.
- `app/src/main.cpp` — branche d'entrée hors-ligne.
- `app/src/tab/download_tab.cpp` — la liste plate reste (accès direct/gestion), mais
  l'espace Téléchargements ouvre désormais les fiches locales.
- `resources/i18n/<14 langues>/main.json` — nouvelles clés (offline mode, grisé, etc.).
- `resources/xml/...` — ajustements mineurs (état grisé episode_card, bandeau offline).

---

## 6. Style de code

Respecter l'existant (`.clang-format` à la racine) :
- Namespace `plex::`, singletons `brls::Singleton<T>`, événements `brls::Event`.
- Réseau/disque **hors** thread principal : `brls::async` pour le travail,
  `brls::sync` pour toucher l'UI (cf. `download.cpp`).
- Idiome `ASYNC_RETAIN` / `ASYNC_RELEASE` pour la capture sûre de `this` dans les
  callbacks asynchrones des vues (cf. `media_movie.cpp`).
- Persistance JSON via `nlohmann` + macros `NLOHMANN_DEFINE_TYPE_*` (cf. `download.hpp`,
  `config.hpp`), pas de nouveau format ni de SQLite.
- i18n via `"clé"_i18n` / `brls::getStr` ; **jamais** de chaîne UI en dur.
- Commentaires : concis, en anglais, expliquant le *pourquoi* (ton du code existant).
- **XML Borealis** : n'utiliser que des attributs connus des `registerXMLAttribute`
  de la classe ciblée — tout attribut inconnu est **fatal à l'inflate** (abort). Se
  demander *quel chemin* inflate le XML modifié.

---

## 7. Commandes

**Build desktop (macOS, machine arm64 — libmpv brew x86_64) :**
```
cmake -B build_desktop -G Ninja -DPLATFORM_DESKTOP=ON -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build_desktop -j 8
```
> ⚠️ La copie de `resources/` dans le bundle est un **post-build du binaire** :
> modifier un XML/i18n **sans** toucher de `.cpp` → `ninja: no work to do` → le bundle
> garde l'ancienne ressource. Pour une itération XML/i18n seule : `cp` direct vers
> `build_desktop/*.app/Contents/Resources/resources/...`.

**Smoke test :** lancer le binaire du bundle (`... .app/Contents/MacOS/... -version`),
puis vérifier le `mtime` du binaire du bundle réellement lancé avant de dire « prêt ».

**Captures / recette visuelle :** harnais `scripts/ui-audit/ctl.py` (scénarios dans
`scripts/ui-audit/scenarios`).

**Autres cibles** (référence, non requises ici) : Switch `./scripts/build-switch.sh`
(Docker) ; Vita cross-compile (Docker `--platform linux/amd64`).

---

## 8. Stratégie de test

Pas de framework de tests unitaires dans le projet → validation **manuelle + visuelle**
via le harnais, plus round-trips ciblés. Scénarios de recette (chacun mappe des AC) :

1. **Film hors-ligne** (AC1, AC5, AC17) : télécharger un film → couper le réseau /
   pointer un serveur mort → parcourir bibliothèque offline → ouvrir la fiche
   (poster/backdrop/logo/résumé/casting présents) → lire le fichier local.
2. **Série partielle** (AC2, AC9, AC10, AC11) : télécharger 2 épisodes d'une saison de
   N → hors-ligne → la série apparaît, la saison liste **tous** les épisodes, seuls 2
   sont lisibles, les autres grisés.
3. **Toujours fiche locale en ligne** (AC6) : connecté, ouvrir un contenu téléchargé
   depuis l'espace Téléchargements → rendu instantané depuis le cache.
4. **Entrée hors-ligne** (AC12–AC16) : serveur injoignable au lancement + catalogue non
   vide → mode hors-ligne (pas `ServerList`) ; catalogue vide → `ServerList` + message.
5. **Parité visuelle** (AC5) : captures fiche **en ligne vs hors-ligne** du même titre,
   comparaison côte à côte via le harnais.
6. **Migration** (AC20) : partir d'un `downloads/index.json` préexistant (fiche
   minimale reconstruite) ; nouveaux téléchargements = fiche complète.
7. **Cycle de vie / stockage** (AC18, AC19) : supprimer un épisode → purge assets ;
   supprimer le dernier épisode d'une saison → purge des nœuds saison/série ; en-tête
   Stockage cohérent.

**Simulation hors-ligne :** couper le Wi-Fi / pointer un URL serveur injoignable /
mode avion (device). Sur desktop, préférer un serveur volontairement éteint.

**Caveats device (mémoire projet) :** Vita/Switch ont des pièges runtime (hwdec,
polices, focus pré-layout) ; toute AC touchant la lecture doit idéalement être
re-vérifiée sur device — signaler explicitement si non testé matériel.

---

## 9. Limites / garde-fous

**Toujours**
- Réutiliser `plex::Item` et les vues existantes ; **un seul** modèle média.
- Conserver la compat ascendante de `downloads/index.json` + migrer au mieux (AC20).
- Builder desktop en `x86_64` et **vérifier le bundle réellement lancé** avant « prêt ».
- Compléter **les 14 langues** i18n pour toute nouvelle clé (fr + en = source de vérité).
- Valider visuellement via `scripts/ui-audit/ctl.py` avant de conclure.
- Réseau/disque hors thread principal (async/sync).

**Demander d'abord**
- Toute rupture du layout disque qui invaliderait les téléchargements d'un utilisateur.
- Ajout d'une dépendance réelle (ex. SQLite) — a priori refusé.
- Refactor de large surface des signatures `plex::getJSON`.
- Toute modification du submodule `library/borealis` / du patch local non commité
  (`scripts/patches/borealis-fixes.patch`) — **ne pas commiter le repo sans traiter ce
  point** (décision fork vs patch-au-build en attente).

**Jamais**
- Écrire une valeur **devinée** vers le serveur (leçon `viewOffset` pollué : la reprise
  locale reste locale).
- Dupliquer `plex::Item` dans un second modèle parallèle.
- Laisser tomber silencieusement une langue i18n.
- Utiliser un attribut XML inconnu de Borealis (fatal à l'inflate).
- Bloquer le thread principal sur du réseau ou du disque.

---

## 10. Risques & points ouverts

- **`to_json` `plex::Item`** : volume de code de sérialisation ; s'assurer du
  round-trip fidèle (surtout `Media/Part/Stream` pour la lecture locale).
- **Champs `librarySection*`** : présence à confirmer sur une réponse serveur réelle
  (sinon dériver la section autrement au download).
- **Ampleur du seam `MediaSource`** : migrer progressivement, surface par surface, en
  gardant `PlexSource` iso-comportement pour éviter les régressions en ligne.
- **Casting hors-ligne** : URLs mixtes (relatives serveur / absolues tmdb) → clé de
  cache robuste.
- **Reprise de lecture hors-ligne** : stocker localement `viewOffset` ? (stretch, à
  arbitrer) — ne jamais le pousser au serveur.
- **Coût disque** des assets additionnels : négligeable vs la vidéo, mais à
  comptabiliser dans l'en-tête Stockage.

---

*Prochaine étape : validation de cette spec, puis planification de la Phase 1
(modèle + capture riche).*
