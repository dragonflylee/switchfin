# Changelog

All notable changes to pleNx are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/) and the
project adheres to [Semantic Versioning](https://semver.org/). Entries are
prepared with [git-cliff](https://git-cliff.org/) from conventional commits
(`git cliff --unreleased --tag vX.Y.Z --prepend CHANGELOG.md`), then curated by
hand. For the history of the upstream project this fork is based on, see the
[Switchfin changelog](https://github.com/dragonflylee/switchfin/blob/dev/CHANGELOG.md).

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
