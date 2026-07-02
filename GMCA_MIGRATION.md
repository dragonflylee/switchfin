# pleNx → GMCA — transition plan

> **Goal:** rename **pleNx** to **Gamepad Media Center Aggregator (GMCA)** to match the app's pivot
> from a Plex-only client to a multi-backend one (Plex · Jellyfin · Emby · Stremio) and its
> multi-device reach (Nintendo Switch · PS Vita · Raspberry-Pi-on-a-TV), **without breaking existing
> users**. Both projects stay online throughout; users migrate at their own pace.

## Naming

- **Display name** (everywhere there is room — Linux launcher, AppStream, README, site, About screen):
  `Gamepad Media Center Aggregator`.
- **Short token** `GMCA` where size-constrained: file names (`GMCA.nro` / `GMCA.vpk` / `GMCA.app`),
  the per-user data/package token, console tile & macOS menu names, title IDs, and the reverse-DNS
  app-id `fun.thcolin.gmca`. Lower-case `gmca` where required (Flatpak app-id, GitHub Pages path).
- **Repo / site**: `github.com/thcolin/gamepad-media-center-aggregator`, Pages at
  `thcolin.github.io/gamepad-media-center-aggregator/`.
- `DMCA` (the obvious "D-pad Media Center Aggregator" acronym) was **rejected**: it collides with the
  copyright-takedown term — SEO-dead, takedown-bait and reads as a piracy tool.

## Why a new repo (not a GitHub rename)

A clean break avoids the two hazards a rename would cause: GitHub Pages project URLs are **not**
redirected on rename (the site would 404), and redirect chains are fragile. pleNx stays online as a
pointer; GMCA starts fresh with its own identity, so the two **install in parallel** on a device and
users switch when they want. Trade-off accepted: the new repo starts with zero stars.

## The four phases (both online throughout)

1. **Ship multi-backend as pleNx `v0.2.0`.** Merge the multi-backend work and tag it — a "pure"
   feature release, *without* announcing the rename (no destination exists yet). This release already
   **pre-migrates user data to the GMCA folder** (see below) so the eventual switch is seamless.
2. **Build GMCA in parallel.** On `feat/rebrand-gmca`: full rebrand (identity, fresh IDs, app-id,
   updater, forwarder, packaging, Android, site, README). Create the repo, wire CI + Pages + release,
   tag **GMCA `v1.0.0`**, publish builds, then submit to hb-app.store (new listing) and VitaDB (new
   entry). At the end of this phase GMCA is downloadable everywhere.
3. **Announce & bridge.** A final pleNx `v0.2.x` release ("pleNx is now GMCA") with a one-time in-app
   notice and release notes pointing to GMCA — the installed pleNx updater is pinned to
   `thcolin/pleNx`, so this is how existing users are told. Add "→ GMCA" banners to the pleNx README
   and site, and post on the forums (GBAtemp, r/SwitchHomebrew, VitaDB).
4. **Coexist, then wind down.** Keep both repos/listings online as long as useful; later archive the
   pleNx repo (read-only, never unpublished) so old links and installs keep resolving.

## Migration mechanics

- **User data** (logins, settings, downloads) — `getPackageName()` names the per-user data folder on
  every platform (`sdmc:/switch/<name>`, `ux0:/data/<name>`, `~/Library/Application Support/<name>`,
  XDG, `%LOCALAPPDATA%`). pleNx `v0.2.0` sets that token to **`GMCA`** (`BUILD_PACKAGE_NAME`, decoupled
  from the still-`pleNx` display name) and chains `AppConfig::init()`'s silent migration
  most-recent-first: `pleNx → GMCA`, then `Switchlex → GMCA` (`fs::rename`, guarded by
  `exists(from) && !exists(to)`). So data relocates to the GMCA folder while the app is still branded
  pleNx; the renamed GMCA build then reads it in place and keeps the same defensive shim for users who
  jump straight from an old pleNx.
- **Console identity is fresh** — new Switch forwarder title ID, Vita `VITA_TITLEID`, PS4
  `PSN_TITLE_ID`, and reverse-DNS app-id → GMCA installs alongside pleNx (no clobbering, no forced
  uninstall), and the Switch HOME-menu name/icon cache can't serve the stale pleNx tile.
- **Updater** points at `thcolin/gamepad-media-center-aggregator`; Linux packages renamed to `gmca`
  with `Provides`/`Conflicts`/`Replaces = plenx` for clean in-place upgrades on Arch/Debian.

## Store notes

- **hb-app.store** keys a listing on the package name → a rename means a **new listing at a new URL**;
  the old pleNx listing's download counter does not carry over. Keep the pleNx page as a pointer.
- **VitaDB** keeps a listing on a numeric id and can be edited in place, but VitaDB-Downloader matches
  updates by **TITLE_ID** — since GMCA uses a fresh TITLE_ID, it is a **new entry**, not an update to
  the pleNx one.
- Both are independent of the in-app updater (GitHub Releases).

## Open TODOs before release

- [ ] **Verify the Switch forwarder title ID `0104474D43410000` is unique** (web / GitHub / GBAtemp
      homebrew list), as was done for pleNx. It is currently a placeholder in `CMakeLists.txt`.
- [ ] **GMCA brand art** — the site and README use a typographic wordmark placeholder; a real logo /
      icon and re-shot screenshots (showing the GMCA name, plus PS Vita and Raspberry-Pi-on-a-TV
      imagery) are still to be produced.
- [ ] Create the `thcolin/gamepad-media-center-aggregator` GitHub repo, enable Pages, port CI secrets.
- [ ] Submit to hb-app.store and VitaDB; add the pointer notices to the pleNx repo/site.
