# pleNx — Refonte UI/UX « Salle obscure »

> Document de référence de la refonte visuelle et ergonomique. Méthode : audit
> par captures → design system → implémentation par lots, chaque lot étant
> vérifié par re-capture (scénarios `scripts/ui-audit/`).

## 0. État d'avancement (2026-06-10)

Validé par captures sur serveur réel : design system or/noir (lots A-B),
fiche film full-bleed (lot C), bibliothèques illustrées par le dernier ajout
(pas de composite sur tous les serveurs), focus poster-only, menu contextuel
X/F4 avec « aller à la série/saison » (sélection de saison ciblée), fiche
personne (`/library/people/{id}/media`), login PIN plein écran (seule méthode),
tutoriel applet recentré sur l'installation de la tuile, lignes parasites des
`brls:Header` supprimées, OSD player (dégradés renforcés, titre complet
« Série · SxEy — Titre », timeline or via thème).

**Recette utilisateur du 2026-06-10 (2e passe)** : aération cartes/rangées
(marge labels 10, inter-rangées 26, `row = largeur×ratio + 55` exact),
trait vertical des titres tué à la racine (frange AA nanovg, §7), ratio 2:3
garanti dans les grilles (`itemImageRatio`/`itemExtraHeight`, recalcul au
layout — recycling_grid.cpp), placeholders « Aucun … » recomposés (bloc
centré icône+texte), fiche acteur enrichie (rôle incarné + pills compteurs,
i18n `person/*` 14 langues), icônes sidebar actives en or (#028087 teal
éradiqué), bibliothèques = entrées de sidebar dynamiques (pattern tico),
crashs `giveFocus` (tap + sortie lecture) et long-press → menu contextuel,
packaging Linux assaini (SVG chevron or, PNG `fun.thcolin.switchlex`,
appdata/desktop/AUR/snap sans mention Jellyfin).

**Bug borealis corrigé (patch local du submodule, à pérenniser par fork) :**
`ScrollingFrame::findTopMostFocusableView` retournait une vue non focusable
(`Box::hitTest` trop permissif) voire une vue d'un arbre détaché → halo
fantôme pleine bannière au premier rendu des fiches + SIGSEGV
(`FiniteTicking::reset` via `onFocusLost`). Deux gardes ajoutées dans
`scrolling_frame.cpp`/`h_scrolling_frame.cpp` (focusable + appartenance à
l'arbre). Pièges documentés : `~/.claude/...memory/switchlex-borealis-pieges.md`
et ci-dessous §7.

**Recette 4 (2026-06-10, 3e-4e passes)** : sidebar 76 px (icône gauche, accent
droit), menu contextuel re-panneau latéral droit pleine hauteur (MenuItem à
icônes conservés), ticker des titres au focus carte uniquement (hooks
`onChildFocus*` de BaseCardCell, broadcasts parents avalés), click_pulse
arrondi + débord 4 px et halo `corner_radius` 14 (patch submodule régénéré :
`scripts/patches/borealis-fixes.patch`), barre d'onglets TOP flottante
(absolute + draw en dernier + hitTest override ; paddings internes 70),
bouton « Téléchargement… (X%) », pills actives texte blanc, onglets
Téléchargements centrés 18, placeholder « Fichiers » (i18n `remote/empty_*`
×14). Vue SAISON refondue tout-scrollable : l'en-tête (pochette/méta/résumé/
bouton IconButton) est la 1re cellule du flow (`SeasonEpisodesDataSource`,
hauteurs FIXES par `heightForRow`) — corrige aussi le **bug « Scrubs S1-8 »**
(saison sans résumé propre + résumé série pas encore connu → TextBox VIDE →
measure func renvoyait le NaN de yoga → layout dégénéré, 0 épisode visible ;
fix `text_box.cpp` + repli `MediaSeason::doSummary()` qui va chercher le
résumé série). Vue PLAYLIST tout-scrollable via
`RecyclingGrid::setHeaderView(view, h)` (en-tête non focusable scrollé,
`grid_header.xml` partagé). Carte « + » en fin de rangées related des fiches
(hub `more=1` → `MoreCardCell` → page complète `HubView` paginée sur la key
du hub). Hub home : playlists non-vidéo filtrées (`Item.playlistType`).
Validé par capture : S2 de Scrubs (22 épisodes, le cas KO) affiche en-tête +
résumé de repli + épisodes, scroll et focus fonctionnels.

## 7. Pièges borealis (vérifiés)

- `brls:Image` `scalingType="fill"` sans `cornerRadius` ne clippe pas (déborde).
- Un seul `ASYNC_RETAIN` par portée (la macro déclare `token`).
- `RecyclingGrid::notifyDataChanged` ne re-binde pas les cellules visibles.
- `brls:DetailCell` n'affiche pas son titre chez nous → `brls:RadioCell`.
- Le halo de focus suit la vue focusable : pour le limiter à l'affiche,
  `getDefaultFocus()` de la cellule renvoie le box image (`BaseCardCell`).
- Un `brls:Rectangle` de largeur 0 dessine quand même ~1px : la frange
  d'antialiasing nanovg d'un path dégénéré. Neutraliser par la couleur
  (`brls/header/rectangle` → transparent, config.cpp), pas par la largeur.
- La hauteur de ligne d'un `RecyclingGrid` est fixe (`itemHeight`) alors que
  la largeur de cellule dérive du conteneur → ratio d'affiche non garanti.
  Utiliser `itemImageRatio`/`itemExtraHeight` (attributs maison, recalcul à
  chaque `reloadData`, recycling_grid.cpp).
- Les hint d'état vide (`hintImage`/`hintLabel`) sont des vues détachées : le
  label n'a pas de hauteur layoutée, lui imposer une boîte au draw.
- Une measure func yoga ne doit JAMAIS renvoyer NaN : en mesure libre yoga
  passe `height=NaN` ; `textBoxMeasureFunc` la renvoyait telle quelle pour un
  texte VIDE → layout parent dégénéré (bug « Scrubs S1-8 »). Texte vide → 0.
  Mieux : hauteurs FIXES par `heightForRow` pour les listes hétérogènes
  (pattern `DownloadDataSource`/`SeasonEpisodesDataSource`).
- `RecyclingGrid::setHeaderView` : le header est un enfant du contentBox SANS
  userdata d'index — chaque boucle sur les children doit le sauter (gardes
  `getParentUserData()` posées). Décalage des cellules via
  `contentTop() = paddingTop + headerHeight`.
- `file(GLOB_RECURSE)` sans `CONFIGURE_DEPENDS` (CMakeLists.txt:75) : un
  NOUVEAU .cpp exige `cmake .` dans le dossier de build, sinon « Undefined
  symbols » trompeur au link.

## 1. Mission et références

L'UI héritée de Switchfin est fonctionnelle mais sans identité ni respiration :
chrome gris, focus cyan borealis par défaut, densité étouffante, hiérarchie
typographique plate. Références retenues :

| Référence | Ce qu'on lui prend |
|---|---|
| **tico** (frontend d'émulation Switch, [ticohq/tico](https://github.com/ticohq/tico)) | Le contenu EST l'interface : l'artwork domine, zéro chrome, header/footer ultra-fins, hints manette, fiche = art quasi plein écran + logo centré. |
| **plezy** (`plezy/lib/theme/mono_theme.dart`) | Design system « mono » : fond #0E0F12, surface #15171C, texte #EDEDED, muted 60 %, outline blanc 8-12 %, radius 8-14, durées 120/200/300 ms, aucun effet superflu. |
| **Plex** (identité) | L'or #E5A00D comme accent unique ; le reste du chrome est neutre et s'efface derrière les affiches. |
| Conventions Switch (tico, wiliwili) | Footer : hints boutons + heure/batterie. Navigation 100 % D-pad, focus toujours visible. |

## 2. Audit de l'existant (2026-06-10)

Captures : `python3 scripts/ui-audit/ctl.py run scripts/ui-audit/scenarios/audit-full.txt`
(sorties dans `/tmp/sx-shots/`). Lecture vidéo : `audit-player.txt` (pousse un
viewOffset réel — validé : direct play OK en desktop).

| Écran | Capture | Problèmes |
|---|---|---|
| Home | `00/01` | Densité bord à bord, compteurs bruts « 18 » dans les titres, barre verticale grise devant chaque titre, focus = simple liseré cyan, aucun accent de marque. |
| Bibliothèques | `02` | Deux rectangles gris nus avec du texte centré — aucun artwork alors que Plex en fournit. |
| Grille films/séries | `03/09` | Onglets texte collés en haut, grille serrée, année grise collée au titre, focus plat. |
| Genres | `06` | Tuiles totalement vides (fond gris + nom dessous). |
| Collections | `07` | Artworks OK mais titres tronqués « Carte Blanche #0 ... », ratio approximatif. |
| Fiche film | `04` | Bonne structure (backdrop+logo+poster) mais : titre principal traité comme un titre de section (avec barre grise), boutons gris plats, casting = portraits carrés collés sans noms lisibles, métadonnées en vrac à droite. |
| Fiche série | `10/11` | Nav saisons quasi invisible (texte minuscule au-dessus de la bannière), liste épisodes sans durée/progression, vignettes collées au bord. |
| Recherche | `12` | Suggestions = liste numérotée brute sans visuels, clavier gris sans style, « Historique » vide déséquilibré. |
| Téléchargements | `13` | État vide correct (icône+texte) ; icône sidebar « nuage » trompeuse. |
| Réglages | `14` | URL plex.direct interminable affichée telle quelle, lignes stock borealis, valeurs vert d'eau. |
| Player OSD | `15/16` | Pas de dégradé de lisibilité, titre sans le nom de la série, timeline fine au curseur minuscule, actions en texte brut (« Sélections Qualité Vitesse de lecture »). |
| Dialogues | `17` | Fond opaque brut, « Valider » vert borealis. |

## 3. Design system « Salle obscure »

Principe : **une salle de cinéma** — tout le chrome est sombre et neutre,
les affiches sont la seule couleur, l'or Plex est le seul accent et marque
toujours « là où on est / ce qui est actif ».

### 3.1 Palette (variante DARK ; LIGHT alignée mais conservée minimale)

| Jeton | Valeur | Usage |
|---|---|---|
| `color/bg` | `#0D0E11` | Fond global (remplace #2A2D32 borealis). |
| `color/surface` | `#16181D` | Cartes, panneaux, sidebar. |
| `color/surface_hi` | `#1E2127` | Surfaces focusées/élevées, dialogues. |
| `color/outline` | `#FFFFFF` α 0.07 | Liserés, séparateurs. |
| `color/text` | `#F1F1F3` | Texte principal. |
| `font/grey` | `#9A9DA5` | Texte secondaire (déjà existant, à réviser). |
| `color/app` (accent) | `#E5A00D` | Or Plex : focus, actif, progression, primaire. |
| `color/accent_hi` | `#F6C12B` | 2ᵉ teinte du halo de focus animé. |
| `color/danger` | `#D2453B` | Suppression/annulation (adouci). |

Surcharges borealis correspondantes (toutes depuis `AppConfig::initThemes()`,
`app/src/utils/config.cpp:658` — `Theme::addColor` écrase la valeur, aucun
patch du submodule) :

- `brls/background` → `color/bg` ; `brls/sidebar/background` → `#101216` ;
- `brls/highlight/color1` → `#E5A00D`, `brls/highlight/color2` → `#F6C12B`
  (le halo « respirant » du focus devient or) ; `brls/highlight/background` → `#1E2127` ;
- `brls/accent`, `brls/sidebar/active_item` → `#E5A00D` ;
- `brls/button/primary_enabled_background` → or, `primary_enabled_text` → `#16130A` ;
- `brls/list/listItem_value_color` → `#C9A86A` (valeurs réglages, plus sobre) ;
- `brls/slider/line_filled`, `pointer_color` → or.

Référence couleurs par défaut borealis : `library/borealis/library/lib/core/theme.cpp:88-140`.

### 3.2 Métriques (Style)

`brls::getStyle().addMetric(...)` au même endroit :

- `brls/highlight/stroke_width` 5 → **4** ; `brls/highlight/corner_radius` 6 → **10** ;
- `brls/highlight/shadow_width/feather/opacity` → ombre plus profonde (cartes « soulevées ») ;
- `brls/animations/highlight` 200 ms (conservé — aligné plezy `normal`).

Défauts : `library/borealis/library/lib/core/style.cpp:31-74`.

### 3.3 Typographie et hiérarchie

Police : `switch_font.ttf` conservée (cohérence plateforme, fallbacks CJK).
La hiérarchie vient des tailles/contrastes, pas de la graisse :

- Titre de fiche : 34 ; titre de rangée : 24 ; titre de carte : 17 ;
  métadonnées : 14 en `font/grey`.
- Suppression systématique des « barres verticales » décoratives devant les
  titres (`brls::Header` borealis) au profit de titres nus plus grands.
- Plus de compteurs bruts (« 18 ») dans les en-têtes de rangées.

### 3.4 Composants

- **Cartes médias** : coins arrondis 10, image pleine carte, titre + sous-titre
  sous la carte (1 ligne, ellipsis), progression = barre or 4 px en bas de
  l'affiche, badge « vu » = pastille or coin supérieur droit.
- **Boutons** : primaire = plein or, texte noir, icône ; secondaires = ghost
  (liseré `color/outline`, texte `color/text`).
- **Tuiles genre** : dégradé bicolore déterministe (hash du nom → palette de
  8 duos), nom EN GRAND dans la tuile, coins 10.
- **Cartes bibliothèque** : 16:9, artwork de section Plex (ou dégradé+icône en
  repli), scrim bas + nom dans la carte.
- **Dialogues** : fond `color/surface_hi`, coins 14, actions : primaire or /
  neutre / danger.
- **OSD player** : dégradés noir→transparent haut (140 px) et bas (180 px),
  titre « Série · SxEy — Titre », timeline 6 px or + poignée 16 px + ticks de
  chapitres, actions = icônes + libellés courts.

## 4. Spécification cible par écran

1. **Home** : padding latéral 40, rangées espacées de 28 ; première rangée
   « Continuer à regarder » en cartes paysage (vignette épisode + progression
   or) — les autres rangées en affiches ; titres de rangées 24 sans compteur.
2. **Bibliothèques** : grandes cartes 16:9 avec artwork, 2 par rangée.
3. **Grilles** : onglets segmentés stylés (pill active or), gouttières 18,
   marge latérale 40.
4. **Fiche film/série** : backdrop avec **fondu vers `color/bg`** (dégradé bas
   + latéral), logo centré, poster arrondi avec ombre, « Lire » = primaire or
   avec icône, métadonnées en pills (année · durée · classification · note ★),
   casting = cartes arrondies avec nom + rôle sur 2 lignes.
5. **Série/saisons** : sélecteur de saisons en segmented horizontal au-dessus
   de la liste ; carte épisode : vignette 16:9 arrondie + n°, titre, durée,
   synopsis 2 lignes, barre de progression.
6. **Recherche** : clavier compact arrondi, suggestions en **grille d'affiches**
   (RecyclingGrid réutilisé), historique en chips.
7. **Réglages** : en-tête profil (avatar rond + nom + nom du serveur « Babylon »
   + pastille connexion) ; l'URL technique reléguée dans un sous-écran.
8. **Player** : OSD ci-dessus ; auto-hide 4 s conservé.
9. **Downloads** : inchangé structurellement (états vides déjà refaits),
   icône de sidebar corrigée (flèche bas), cartes au ratio affiche (déjà fait).

## 5. Plan d'implémentation (par lots, avec vérification visuelle)

| Lot | Contenu | Fichiers principaux |
|---|---|---|
| A — Fondations | Palette + surcharges brls + métriques highlight ; suppression des compteurs/barres de titres de rangées ; espacements home/grilles | `app/src/utils/config.cpp:658`, `app/src/tab/home_tab.cpp`, XML `resources/xml/tabs/*.xml`, `app/src/view/recycling_grid.cpp` |
| B — Cartes | Coins arrondis + progression or + badge vu sur `VideoCardCell`, cartes paysage « Continuer à regarder » | `resources/xml/view/video_card.xml`, `app/src/view/video_card.cpp`, `app/src/view/video_source.cpp` |
| C — Fiches | Dégradé backdrop→fond, boutons or/ghost, pills de métadonnées, casting arrondi+rôles, sélecteur saisons | `resources/xml/tabs/movie.xml`, `series.xml`, `app/src/tab/media_movie.cpp`, `media_series.cpp` |
| D — Bibliothèques & genres | Cartes 16:9 artwork section, tuiles genre dégradées | `app/src/tab/media_folder.cpp` (+XML), `media_collection.cpp` |
| E — Recherche | Suggestions en grille d'affiches, clavier restylé, chips historique | `app/src/tab/search_*.cpp` (+XML) |
| F — Réglages | En-tête profil, URL reléguée, groupes aérés | `app/src/tab/setting_tab.cpp` (+XML) |
| G — Player | Dégradés OSD, titre complet, timeline or épaisse + chapitres, actions icônes | `app/src/view/video_view.cpp` (+XML player) |
| H — Sidebar | Labels + pill active or, icône downloads, logo | `app/src/activity/main_activity.cpp`, XML activité principale |

Chaque lot : build desktop → scénario d'audit → comparaison avant/après →
ajustement. Build : `cmake --build build_desktop -j 8`
(`-DCMAKE_OSX_ARCHITECTURES=x86_64` requis sur cette machine).

## 6. Harnais de test UI

- `scripts/ui-audit/ctl.py` : pilote l'app desktop au clavier (osascript) et
  capture la fenêtre (screencapture). Permissions macOS requises :
  Accessibilité + Enregistrement d'écran pour le terminal.
- Mapping clavier borealis desktop (`glfw_input.cpp:266`) : flèches = D-pad,
  Entrée = A, Échap = B. **Limite : X/Y inaccessibles au clavier** (X = clic
  milieu souris) → les états « tri (Y) » et menus contextuels (X) se testent à
  la souris ou après ajout d'un mapping (à décider).
- Scénarios versionnés : `scripts/ui-audit/scenarios/audit-full.txt` (15 états,
  sans effet de bord) et `audit-player.txt` (lecture réelle, pousse un
  viewOffset au serveur).
- L'écran de connexion (PIN) se testera en sauvegardant/restaurant
  `~/Library/Application Support/Switchlex/config.json` (ne pas déconnecter
  sans backup).
