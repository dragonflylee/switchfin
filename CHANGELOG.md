# Changelog

All notable changes to GMCA (formerly pleNx) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/) and the
project adheres to [Semantic Versioning](https://semver.org/). Entries are
prepared with [git-cliff](https://git-cliff.org/) from conventional commits
(`git cliff --unreleased --tag vX.Y.Z --prepend CHANGELOG.md`), then curated by
hand. For the history of the upstream project this fork is based on, see the
[Switchfin changelog](https://github.com/dragonflylee/switchfin/blob/dev/CHANGELOG.md).

## [1.0.3] - 2026-07-12

### Added

- **Now Playing — gamepad seek and a visible, reorderable queue (#11).** Scrub
  through the current track with the controller, and see the up-next queue right
  on the Now Playing screen — reorder it in place with the gamepad.

### Fixed

- **Switch HOME tile crashed on launch ("an error occurred") after installing
  it on a self-updated install.** 1.0.1's "stage the NRO to `GMCA.nro`" step used
  `std::filesystem::copy_file`, which fails on the console and left a **0-byte
  `GMCA.nro`**; the forwarder then loaded that empty file (and, because it
  existed, never fell back to the real NRO). Removed the in-app copy entirely —
  the forwarder now falls back to the self-updated `pleNx.nro` on its own — and
  hardened the forwarder to **skip any candidate that isn't a valid NRO** (magic
  check), so a leftover 0-byte `GMCA.nro` is ignored instead of crashing.
- **The Switch boot screen still showed the old "pleNx" logo (top-left).** The
  forwarder's boot logo (`NintendoLogo`) was never rebranded during the
  pleNx → GMCA move; it's now blank (only the standard Nintendo Switch startup
  logo remains).

### Changed

- **Guided pleNx → GMCA HOME-tile migration.** Migrated users are prompted once
  on the first GMCA launch to install the GMCA HOME tile; after installing it, a
  message points them to remove the leftover "pleNx" tile from System Settings ›
  Data Management (an installed tile can't be rebranded or deleted for you).

## [1.0.2] - 2026-07-12

### Fixed

- **Switch HOME tile showed no icon and refused to launch (error 2016-1257,
  "the console must be updated").** The app icon regenerated during the rebrand
  grew to 1024×1024 and carried an XMP metadata profile; the HOME menu is strict
  about the forwarder's control icon (the album is lenient, which is why it
  looked fine there). Regenerated `resources/icon/icon.jpg` as a clean 256×256
  baseline JPEG with all metadata stripped — the format the pleNx forwarder used.

## [1.0.1] - 2026-07-12

Fixes the HOME-tile experience for Switch users who self-updated from pleNx to
GMCA 1.0.0.

### Fixed

- **Switch HOME tile after a pleNx → GMCA self-update.** The in-app updater
  replaces the NRO in place, so migrated users kept a binary named `pleNx.nro`
  and an old "pleNx" forwarder tile, while the new GMCA forwarder only looked
  for `GMCA.nro` — installing the GMCA tile would fall through to hbmenu.
  - The GMCA forwarder now also resolves the legacy `sdmc:/switch/pleNx.nro`
    (and `pleNx/pleNx.nro`) so an installed tile launches the app regardless.
  - Installing the HOME tile now stages the running NRO to the canonical
    `sdmc:/switch/GMCA.nro` first.
  - Migrated users, who were past the one-time pleNx-era install prompt, are
    re-offered the GMCA tile once (a GMCA-era prompt, gated separately).

## [1.0.0] - 2026-07-12

First release under the new name **GMCA — Gamepad Media Center Aggregator**
(formerly pleNx). This milestone opens the app to multiple media servers and
adds music, on top of the 0.1.x work listed below.

### Rebrand — pleNx is now GMCA

- The app, packaging, website and in-app identity are rebranded to GMCA. Your
  logins, settings and downloads migrate automatically on first launch —
  nothing to re-configure. On Linux the `gmca` package supersedes `plenx` in
  place; on console GMCA installs alongside an existing pleNx (fresh title IDs).

### Multi-backend media servers

- **Jellyfin, Emby and Stremio** join **Plex**. Choose the server type when
  adding a connection; home, libraries, search, playback, downloads and the
  connection switcher all work across every backend.

### Music (#11)

- Browse and play a music library — **Artist → Album → Track** — from Plex,
  Jellyfin or Emby, with a dedicated **Now Playing** screen and a persistent
  **mini-bar** that keeps playing across the app and in the background (screen
  off / HOME). Reuses the existing MPV playback engine.

### Notes

- Consolidates the 0.1.11–0.1.15 releases below — offline download browsing,
  folder sorting/pinning, Blu-ray folder playback, the PS Vita in-app
  self-update and the Vita transcode/DNS fixes are all included.

## [0.1.15] - 2026-07-11

### Offline downloads (#19)

- **Browse your downloads with no connection, just like online.** Downloaded
  movies and shows now get a full local catalogue: their complete fiche
  (summary, genres, ratings, cast) plus poster, backdrop, logo and cast photos
  are cached to disk at download time, so a downloaded title renders offline
  exactly as it does against the server. Shows keep their season/episode
  structure — the season lists every episode, and episodes you did not download
  are greyed out and not playable.
- **A real offline mode.** When the server is unreachable at launch and you have
  downloads, pleNx now opens straight into an offline shell — sidebar, home rows
  and library grids built from the local catalogue — instead of the server
  picker. A **Retry** affordance reconnects you when the server is back.
- **Offline Search** looks through your local catalogue; **Watchlist** and
  **Playlists** show a clear "unavailable offline" state. Deleting a download
  prunes its catalogue entry and cached artwork (cascade).
- The downloaded fiche is preferred even while online, so opening a downloaded
  title is instant and plays the local file.

### Files

- **Sort folder listings** (#23) — a Y "Sort" panel orders any local/remote
  folder by name, date or size (ascending/descending); ".." stays first and
  folders always sort above files.
- **Pin local folders** (#24) — pin the focused folder to the Files root
  (X / F4) for one-tap access; pins persist in `config.json`. The home folder is
  now exposed on macOS/Linux.

### Blu-ray

- **Play Blu-ray/BDMV folder backups from local storage** (#18): a folder
  holding a `BDMV/STREAM` backup now offers a "Play Blu-ray" entry at the top of
  its listing.

### Bug fixes

- **Downloads tab** — the Storage card now scrolls with the list (it was a
  pinned header that also crashed the offline Files tab); failed downloads can
  be retried; the "Downloaded" button is actionable; cards show a downloaded
  badge; the scroll indicator sits flush with the window edge.
- **File browser** — dropped an oversized loading-skeleton flash when changing
  folders.
- **Consistent placeholders** — fiche posters (movie/series/season) and episode
  thumbnails without artwork now show the media icon instead of an empty
  rectangle, and episode cards keep their size (no collapsed focus halo).
- Lock callbacks on the shared curl DNS cache — fixes a latent concurrent crash.
- New i18n keys added across all 14 languages.

## [0.1.14] - 2026-07-05

### PS Vita

- **The in-app updater now installs the new version directly instead of opening
  a broken browser page.** When an update was available, the Vita opened the
  GitHub release page in the system browser — a page its ageing WebKit cannot
  render, so it hung and the download never showed up. The Vita now self-updates
  in place like the Switch does: it downloads the VPK, unpacks it and installs
  it through the system package installer, then drops you back to the LiveArea to
  relaunch the updated bubble. (The VitaDB "downgrade to 0.1.4" prompt reported
  alongside this is a stale VitaDB store entry, not a pleNx bug; see
  `RELEASING.md`.)

## [0.1.13] - 2026-07-03

### Interface

- **Series and movie logos no longer smear.** The transparent title logos on
  detail pages are drawn in "fit" mode, so the image is smaller than its box
  and sits letterboxed inside it. The empty area around it was still being
  painted with the image, which sampled outside the texture and — because edge
  pixels are clamped — dragged vertical streaks down from every shape. The
  image now fills only its own fitted area, so logos render cleanly. Affects
  every platform (the fix is in the shared image renderer).

## [0.1.12] - 2026-07-03

### Player

- **Changing the audio or subtitle track no longer stops playback with a
  "playback error" on PS Vita.** In transcode mode a track change reloads the
  stream, but the reload happened in place and never released the hardware
  decoder, so it stalled and then failed. The reload now resets the player
  first (like episode navigation) and tears the previous transcode session
  down on the server instead of leaving it orphaned.
- **Audio and subtitle tracks are now selectable during local playback from
  the file browser.** The player showed the track buttons but had no handler
  wired, so they appeared to do nothing.
- **A failed transcode now falls back to direct play once**, instead of going
  straight to an error — a smoother recovery on constrained hardware.
- **Playback errors now show the underlying mpv reason** (e.g. "unrecognized
  file format") instead of a bare "playback error", making bug reports
  actionable.

### Fixes

- **PS Vita: the transcoder height is capped at 1080p**, the hardware decoder's
  limit, so a 4K source at a high bitrate no longer produces a stream the Vita
  cannot decode.

## [0.1.11] - 2026-07-03

### Fixes

- **PS Vita: servers reached by a hostname no longer stall with an endless
  "timeout reached".** libcurl was built with the synchronous name resolver, so
  a DNS lookup issued from a background thread could not be bounded by any
  timeout — the synchronous resolver can only abort a slow `getaddrinfo()` via
  `SIGALRM`, which libcurl arms on the main thread alone. A slow-to-resolve
  hostname (e.g. a duckdns address) therefore hung every request indefinitely,
  and raising the request-timeout setting had no effect; only a raw LAN IP,
  which skips name resolution, worked. curl is now built with the threaded
  resolver so lookups run in a worker thread the timeout can abandon, and
  `CURLOPT_NOSIGNAL` keeps libcurl off signals on its background threads.

## [0.1.10] - 2026-06-15

### Fixes

- **PS Vita: movie logos no longer render sheared or with a torn vertical
  strip.** GPU texture compression always extracted a full 4×4 pixel block, so
  for images whose width or height isn't a multiple of 4 the edge blocks pulled
  in the next row's pixels (the visible vertical corruption) or read past the
  decoded image. Edge blocks are now clamped to the valid pixels, with the
  border replicated into the padding for a clean edge

## [0.1.9] - 2026-06-15

### Fixes

- **Self-hosted servers on your LAN are reachable again when `plex.direct` DNS
  is blocked.** Server discovery only probed the `plex.direct` hostname, whose
  public DNS record points at a private IP — which routers and resolvers with
  DNS-rebinding protection (Fritz!Box, Pi-hole, AdGuard…) refuse to resolve. A
  server sitting on the same network then reported "unreachable" even though it
  answered directly, while the official clients connected fine. pleNx now also
  probes the raw IP address for private, CGNAT and VPN endpoints, bypassing
  `plex.direct` entirely — and re-discovers a working address on reconnect when
  the server changes IP (e.g. a new DHCP lease)

## [0.1.8] - 2026-06-15

### Downloads

- **The download card now shows transfer speed and ETA.** While a download is
  running, the progress line reads `123 MB / 456 MB · 5.2 MB/s · 12:30 left`;
  the rate is a smoothed average (EMA) so the estimate stays steady instead of
  jumping on every tick

### Library

- **Movie and show pages show critic and audience ratings with their official
  icons.** Plex's rating sources are mapped to the Rotten Tomatoes tomato
  (fresh/rotten), the audience popcorn (upright/spilled), and the IMDb and TMDb
  marks, each sized to its own aspect; a plain star is kept as a fallback when
  the source is unknown

### Changelog

- **The changelog is now readable in the app**, from Settings ▸ Changelog — the
  full history, scrollable
- **The update prompt shows the new version's release notes** instead of just
  its number, so you can see what changed before updating

## [0.1.7] - 2026-06-15

### Sign-in

- **Selecting a server no longer looks frozen.** Picking a server (or profile)
  during Plex sign-in blocked all input while probing the server's connections
  — several seconds, with nothing on screen — so the dialog just vanished and
  the app seemed dead. A loading overlay now sits over the pairing screen for
  the whole wait, with a reassurance line that fades in if it runs long
- **Account linking survives a transient network hiccup.** A single timeout or
  TLS blip (common on Vita) no longer aborts the whole PIN flow; only an
  expired/consumed code is terminal, and the 2-minute deadline stays the real
  give-up

### Player

- **Direct-access OSD.** The speed control is replaced by icon buttons for
  audio track, subtitle track and quality. Subtitles get a live, translucent
  sub-delay overlay (LEFT/RIGHT) on top of the video; audio, subtitle and
  sub-sync leave the settings panel. Video quality is now remembered and
  defaults to a 4 Mbps transcode on Vita (its decoder chokes on heavy direct
  play), staying Auto everywhere else
- **Marking watched is now a targeted card refresh** instead of reloading the
  whole view (heavy on the home screen): the single card updates optimistically

### Fixes

- **The synopsis no longer collapses to a single line** when returning from a
  person page — a relayout had cut the text at an intermediate width. The
  action row also gains a little breathing room below the title/meta block
- **The splash logo no longer shows a flat colored band along its edges** — an
  edge-clamp artifact from letterboxing the non-square asset, fixed by
  re-exporting it square with a transparent margin
- **Deleting the last card in a grid no longer leaves a ghost highlight**, and
  emptying the downloads list hands focus back to the sidebar

## [0.1.6] - 2026-06-12

### Fixes

- **PS Vita: transparent images no longer render as opaque black blocks.**
  Every non-WebP image was GPU-compressed to DXT1, a format with no alpha
  channel, so transparent PNGs (movie clear-logos) exposed the RGB residue
  hidden under their transparent pixels. Images with real transparency now
  use DXT5 (8-bit alpha); opaque ones keep the compact DXT1

## [0.1.5] - 2026-06-11

### Fixes

- **Controller navigation no longer dies after closing the player.** The
  post-playback refresh destroyed the very view the focus had just returned
  to, leaving the app button-deaf (only touch worked — a docked Switch was
  stuck). The focus now lands back on the first home row, visibly
- **Docking the Switch mid-playback now switches the video to fullscreen
  1080p** instead of leaving it small in a corner (inherited upstream
  regression: the mpv render target never followed the framebuffer swap)

### Player

- Focused controls now show a **translucent orange background** instead of
  the border halo — far more readable over moving video
- The close button is a round, properly centered icon
- **Up/down wake the OSD** and focus the play/pause button
- **The progress bar is focusable**: left/right seek from it with
  accelerating steps (TV-style OSD is now the default, still toggleable)
- Removed the always-on bottom progress bar and its settings

## [0.1.4] - 2026-06-11

### Fixes

- **Every truncated row now ends with a "+" card** opening the full list:
  the home rows (Continue Watching and all server hubs, e.g. "Recently
  Added") and the Suggestions tabs of movie and show libraries were missing
  it — only the related rows of detail pages had one. The card shows
  whenever the server reports more content than displayed

## [0.1.3] - 2026-06-11

### Breaking

- **New Switch title ID for the HOME menu tile (`0104201312000000`)**: the
  previous ID was inherited from Switchfin-era builds, and the HOME menu kept
  serving its cached "Switchlex" name and icon for that title even after
  reinstalling and rebooting. A fresh ID starts clean. **Migration**: delete
  any old tile ("Switchlex" or a previous pleNx) from the HOME menu (tile →
  <kbd>+</kbd> → Manage Software → Delete), replace `pleNx.nro` on the SD
  card, then reinstall the tile from the app
- The forwarder NPDM `program_id` is now generated from the single
  `PROJECT_TITLEID` source of truth instead of a hardcoded copy that could
  drift

### Fixes

- **Branding harmonized across every derived asset**: the X mark color order
  (cyan top, red bottom) now matches the reference logos everywhere — app
  icons (Switch NACP, macOS icns, Windows ico, Linux hicolor, PS4), the
  forwarder wordmark, the scalable icon wrapper and the website favicons /
  social image. The Android launcher icons and the PS4 / PS Vita system
  images still carried the upstream Jellyfin artwork and now ship pleNx
  branding (Vita assets kept 8-bit indexed as the firmware requires)

### Website

- The hero opens with the app icon presented as a homebrew-menu tile next to
  the wordmark, and the floating nav no longer flashes before first paint

## [0.1.2] - 2026-06-11

### Fixes

- **Translations completed and corrected across all 13 languages**: every
  locale (cs, de, es, fr, ja, ko, pt, ru, tr, uk, vi, zh-Hans, zh-Hant) is now
  at full key parity with the English reference. Filled in missing strings
  (the in-app updater messages, the offline download block, and large gaps in
  the German, Ukrainian and Vietnamese settings screens), translated the Plex
  sign-in and HOME-tile hint screens that had been left in English in several
  locales, fixed mistranslations and typos, and replaced the stale `failed`
  hint key with the `retry` action used by the retry dialog. The French
  "current speed" overlay also regained its `{}` placeholder so the playback
  speed value is shown again.

## [0.1.1] - 2026-06-11

### Features

- **Working in-app self-update on Nintendo Switch**: when a new release is
  published, pleNx offers to update itself (checked at startup and from
  Settings → "Check for updates"), downloads the NRO with a live progress
  dialog (cancelable), verifies the file size against the GitHub release
  asset before touching anything, then replaces itself and prompts to
  relaunch. On the other platforms the dialog opens the release page in the
  browser.

### Fixes

- The updater inherited from Switchfin always wrote the new NRO to
  `sdmc:/switch/pleNx/pleNx.nro`, while the HOME tile (forwarder) and most
  manual installs launch `sdmc:/switch/pleNx.nro`: an update could silently
  leave the old version running forever. The app now replaces the NRO it was
  actually launched from (`argv[0]`). ⚠️ As a consequence, updating **from
  0.1.0** through the in-app dialog may appear to have no effect depending on
  the NRO location — 0.1.0 users with the NRO at `sdmc:/switch/pleNx.nro`
  should replace it manually once.
- The release-asset download URL is no longer hardcoded: URL and expected
  size are taken from the GitHub API response
- The startup update check now honors its intended delay instead of racing
  the plex.tv login requests
- All references to the repository now use its new name `thcolin/pleNx`
  (the updater, the README, the Debian/Flatpak/AUR metadata) — the GitHub
  redirect from `thcolin/plenx` kept everything working, but the canonical
  name is safer long-term
- The Arch package version is now derived from `CMakeLists.txt` instead of
  being hardcoded in the PKGBUILD (the 0.1.1 package was previously
  versioned 0.1.0)

## [0.1.0] - 2026-06-11

First release of **pleNx**, a third-party native [Plex](https://www.plex.tv/)
client for Nintendo Switch, macOS, Windows, Linux, Android, PS4 and PS Vita.

pleNx is a fork of [Switchfin](https://github.com/dragonflylee/switchfin)
(a Jellyfin client) fully migrated to the Plex API, with a complete UI overhaul.

### Features

- **[breaking]** Full migration from the Jellyfin API to the Plex API
  (Switchfin → pleNx): sign-in via the plex.tv PIN flow (link code), server
  discovery and connection selection, libraries, home hub, movies, shows,
  collections, search, and playback (direct play and transcode) backed by mpv
- Complete UI overhaul with the new pleNx branding: icon-only sidebar,
  full-bleed detail pages (backdrop banner with title logo, inline action
  buttons, director-first cast row), fully scrollable season / playlist /
  collection views, redesigned search (fixed keyboard, history, suggestions),
  quick-actions side panel on any poster (<kbd>X</kbd> or long-press),
  guaranteed 2:3 poster ratios, structured skeletons and empty states,
  pill-shaped toasts, focus halo clipped to its scroll frame
- **Plex Watchlist**: dedicated sidebar tab with provider-backed sorting and
  filters (movies/shows, on-server availability — off-server posters are
  dimmed), add/remove from detail pages and the quick-actions menu
- **Offline downloads**: storage gauge, active/completed sections, one-tap
  season or whole-show download, local playback
- **Remote file servers managed from the UI**: add/edit/delete WebDAV, FTP,
  SFTP and HTTP(S) shares with a mandatory connection test before saving
- Genre cards illustrated with the [Kometa](https://kometa.wiki/) default
  posters (proxied and resized by the Plex server, nothing bundled)
- Playlists (custom posters honored, square cards), collection pages with
  item-count headers, "see all" card opening full hub pages, person pages,
  "Play again" on fully-watched shows, connection loading screen with async
  server probing (no more frozen first frame)
- Available in 14 languages
- Nintendo Switch specifics: working applet-mode warning screen with HOME
  tile (forwarder) installation flow, system Standard font by default
  (consistent accented glyph spacing), USB drives via libusbhsfs

### Fixes

- Infinite relayout loop on fractional layout widths that emptied grids,
  killed controller navigation and crashed the app on both desktop and Switch
- Ghost focus on recycled or detached views: the focus memory is now
  validated and purged in the borealis layer (focus could previously land on
  invisible cells, e.g. when returning from the sidebar)
- Empty-text yoga measurements returning NaN (seasons without a summary
  rendered a full-screen header and zero episodes)
- All vertical views now use "centered" scrolling: the previous "natural"
  mode could trap or steal the controller focus at scroll edges
- Borealis framework fixes shipped as a patch applied on top of the submodule
  (focus guards, keyboard sticky keys, click animation overshoot, concentric
  highlight, Switch shared-font order, notification restyle)

### Build

- Local Nintendo Switch build via Docker (`scripts/build-switch.sh`,
  devkitPro image, deko3d driver by default, builtin NSP forwarder)
- Continuous delivery via GitHub Actions: pushing a `v*` tag builds all
  platforms (Switch NRO, macOS DMG, Windows, Linux Flatpak/Arch, Android APK,
  PS4 PKG, PS Vita VPK) and drafts a GitHub release with notes taken from this
  changelog — the borealis patch is applied in every CI job, and the in-app
  update checker performs a real semantic version comparison

### Documentation

- Jellyfin → Plex migration plan (`PLEX_MIGRATION.md`), UI redesign reference
  (`UI_REDESIGN.md`), refreshed README with feature tour and screenshots
