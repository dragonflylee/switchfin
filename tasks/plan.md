# Plan d'implémentation — Navigation hors-ligne (issue #19)

Dérivé de `SPEC.md`. Ordre = dépendances. Chaque tâche = 1 commit atomique.

> ⚠️ **Contrainte de vérification** : le projet a `BUILD_TESTING OFF` et **aucun
> framework de test**. La boucle TDD stricte « 1 test qui échoue par tâche » n'est
> pas applicable au code UI/Borealis. Adaptation retenue :
> - **Tests logique autonomes** (petit binaire C++ compilé à part) pour les briques
>   *pures* : round-trip `to_json`/`from_json`, regroupement du catalogue, migration.
> - **Compilation** (`cmake --build`) comme garde-fou à chaque tâche.
> - **Revue par agent en background** par phase.
> - **Recette visuelle/device** (UI, offline, lecture) = **non auto-certifiable** →
>   à la charge de l'utilisateur (harnais `scripts/ui-audit/ctl.py` + serveur réel +
>   Switch/Vita).

## Phase 1 — Modèle + capture riche
- **T1** — `plex::Item::to_json` + structs imbriqués (`Media/Part/Stream/Role/Chapter/
  Marker/Section`) ; ajout champs `librarySectionID`/`librarySectionTitle` (à
  confirmer serveur). *Test logique : round-trip Item.* (AC1, AC20)
- **T2** — `OfflineCatalog` (`offline_library.hpp/.cpp`) : modèle de nœuds, persistance
  `catalog.json` + `meta/{ratingKey}.json`, API lecture (`sections/sectionItems/
  metadata/children/allLeaves`), migration best-effort depuis `index.json`. *Tests
  logique : regroupement libs→séries→saisons→épisodes, migration.* (AC2, AC10, AC12, AC20)
- **T3** — Pipeline download enrichi : capture fiche complète + ancêtres (saison/série)
  + children complets (épisodes grisables) + assets (art/logo/poster/casting),
  best-effort ; série entière sans fetch redondant. (AC1–AC4)
- **T4** — Purge en cascade (suppression → assets/meta orphelins, nœuds vides) +
  comptabilité stockage. (AC18, AC19)

## Phase 2 — Cache image hors-ligne
- **T5** — Cache disque `downloads/art/` + `art/index.json` ; branche offline dans
  `Image::load` (résolution locale). (AC17)

## Phase 3 — Seam MediaSource + rendu local
- **T6** — Interface `MediaSource` + `PlexSource` (iso-comportement en ligne) +
  `OfflineSource` (lit le catalogue). `MediaSource::current(context)`. (AC5, AC6)
- **T7** — Router fiches film/série/saison via `MediaSource` ; rendu local ; masquage
  « similaires » + Watchlist hors-ligne. (AC5–AC8)
- **T8** — Router grilles/collection ; épisodes non téléchargés **grisés** &
  non-lisibles ; rangée saisons filtrée. (AC9, AC10, AC11)

## Phase 4 — Mode hors-ligne global
- **T9** — `NetworkState` + branche d'entrée `main.cpp` : serveur injoignable +
  catalogue non vide → MainActivity offline (sinon `ServerList` + message). (AC12, AC16)
- **T10** — Sidebar/Accueil/Recherche offline depuis le catalogue ; Watchlist/Playlists
  masqués/état vide ; Remote/UMS/Settings inchangés ; toggle « repasser en ligne ». (AC13–AC15)

## Phase 5 — Finition
- **T11** — i18n **14 langues** (nouvelles clés) + états vides + ajustements XML
  (episode_card grisé, bandeau offline).
- **T12** — Revue background finale + recette harnais + build release final.

## Actions terminales (demandées, sous condition « si tout passe »)
- Commit par tâche, PR vers `dev`, revue agent background.
- **Merge + réponse au ticket + close #19 + « dispo prochaine version »** →
  **irréversible/outward-facing** : à confirmer explicitement (voir checkpoint).
