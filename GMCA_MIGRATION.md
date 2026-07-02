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

## Why rename the repo (not create a new one)

Renaming `thcolin/pleNx` → `thcolin/gamepad-media-center-aggregator` **keeps the stars, watchers,
forks, issues and history**, and GitHub auto-redirects clone/fetch/push, the API and web URLs. A
valuable side effect: the installed pleNx updater is pinned to `thcolin/pleNx`, so after the rename the
API redirect means existing installs **find the GMCA releases automatically** — the update bridge is
almost free. Two costs, both minor: GitHub Pages project URLs are **not** redirected, so
`thcolin.github.io/pleNx/` will 404 (the site content already lives at the new path — only old inbound
links break); and the freed `pleNx` name **must not be reused** (reusing it breaks the redirects).

Note this is separate from the **app** identity: the console title IDs and reverse-DNS app-id are kept
**fresh** (not pleNx's), so GMCA still installs *in parallel* with an existing pleNx app on a device
and escapes the Switch HOME-menu name/icon cache — data is carried over by the migration shim. Repo
continuity (stars) and a clean app install are thus both achieved.

## The transition

1. **(Optional) Ship multi-backend as pleNx `v0.2.0` first.** Tag the multi-backend work under the
   current name — a "pure" feature release that also **pre-migrates user data to the GMCA folder** (see
   below). Softens the change by getting multi-backend out before the rename. Can be skipped: GMCA
   `v1.0.0` carries the same shim and reaches pleNx users via the updater redirect anyway.
2. **Rename the repo & release GMCA `v1.0.0`.** Rename `thcolin/pleNx` in GitHub settings, push the
   `feat/rebrand-gmca` branch, let Pages redeploy at the new path. Tag **GMCA `v1.0.0`**, publish
   builds, then submit to hb-app.store (new listing — its slug is name-based) and VitaDB (new entry —
   auto-update matches on the fresh TITLE_ID). GMCA is now downloadable everywhere and the redirected
   updater already serves it to existing pleNx installs.
3. **Announce.** GMCA `v1.0.x` (or the notes of `v1.0.0`) carries a one-time in-app "pleNx is now
   GMCA" notice for clarity — the mechanical bridge is automatic, this is just messaging. Update the
   README/site and post on the forums (GBAtemp, r/SwitchHomebrew, VitaDB).

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
- [ ] **Rename** `thcolin/pleNx` → `thcolin/gamepad-media-center-aggregator` in GitHub settings, push
      `feat/rebrand-gmca`, and let Pages redeploy at the new path. Do **not** create anything at the
      freed `pleNx` name (it would break the redirects).
- [ ] Submit to hb-app.store (new listing) and VitaDB (new entry).
