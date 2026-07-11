# pleNx → GMCA — transition plan

> **Goal:** rename **pleNx** to **Gamepad Media Center Aggregator (GMCA)** to match the app's pivot
> from a Plex-only client to a multi-backend one (Plex · Jellyfin · Emby · Stremio) and its
> multi-device reach (Nintendo Switch · PS Vita · Raspberry-Pi-on-a-TV), **without breaking existing
> users**. Both projects stay online throughout; users migrate at their own pace.
>
> **Decisions taken:** ship **GMCA `v1.0.0` directly** (no intermediate pleNx `v0.2.0` — the data
> migration shim is already embedded in `v1.0.0` and covers the direct jump). The branch was cut
> from the older `v0.1.10`, so `dev` was **integrated by merging `origin/dev` into
> `feat/rebrand-gmca`** (done — merge commit `a7ae506`; see **B1**).

## Naming

- **Display name** (everywhere there is room — Linux launcher, AppStream, README, site):
  `Gamepad Media Center Aggregator`. *(Note: the in-app About screen currently shows the short token
  `GMCA`, not the full name — see TODOs.)*
- **Short token** `GMCA` where size-constrained: file names (`GMCA.nro` / `GMCA.vpk` / `GMCA.app`),
  the per-user data/package token, console tile & macOS menu names, title IDs, and the reverse-DNS
  app-id `fun.thcolin.gmca`. Lower-case `gmca` where required (Flatpak app-id, GitHub Pages path).
- **Repo / site**: `github.com/thcolin/gamepad-media-center-aggregator`, Pages at
  `thcolin.github.io/gamepad-media-center-aggregator/`.
- `DMCA` (the obvious "D-pad Media Center Aggregator" acronym) was **rejected**: it collides with the
  copyright-takedown term — SEO-dead, takedown-bait and reads as a piracy tool.

## Why rename the repo (not create a new one)

Renaming `thcolin/pleNx` → `thcolin/gamepad-media-center-aggregator` **keeps the stars, watchers,
forks, issues and history** (confirmed in GitHub's docs: *"all existing information, with the
exception of project site URLs, is automatically redirected to the new name, including Issues,
Wikis, Stars, Followers"*), and GitHub auto-redirects clone/fetch/push, the API and web URLs.

**The update bridge is the key side effect — and it is verified, not assumed.** Installed pleNx
binaries have `git_repo = "thcolin/pleNx"` compiled in (`v0.1.14:app/include/utils/config.hpp:20`)
and call `https://api.github.com/repos/{git_repo}/releases/latest` (`app/src/utils/version.cpp:248`).
After the rename:
- The **REST API 301-redirects the full path**, `/releases/latest` included. Verified live on real
  renames: `curl -sI api.github.com/repos/joyent/node/releases/latest` → `301` to
  `.../repositories/211666/releases/latest` (target is the **stable numeric repo id**, immune to
  future renames). The redirected JSON already carries `browser_download_url`s pointing at the **new**
  name, so the asset download needs no second redirect.
- The client **follows the redirect**: `curl_easy_setopt(this->easy, CURLOPT_FOLLOWLOCATION, 1L)`
  (`app/src/api/http.cpp:116`), used by every `HTTP::get`/`HTTP::download`.

> **Correction to an earlier draft:** the commit `daa5da7` (`plenx` → `pleNx`) is **not** evidence of
> the redirect. That was a **case-only** change = the same repo (`api.github.com/repos/thcolin/plenx`
> → **HTTP 200**, not 301); it never exercised a redirect. The proof above comes from *actual* renames.

**Two costs, both minor and web-only.** GitHub Pages **project URLs are not redirected**, so
`thcolin.github.io/pleNx/` will 404 (verified: `thcolin.github.io/switchlex/`, this account's
older name, is a hard 404 today). And the freed `pleNx` name **must never be reused** — see below.
Neither affects the app: existing installs update through the API + Releases, which *are* redirected.

### ⚠️ Do NOT create anything at the freed `pleNx` name — this is the answer to "should I keep a pleNx repo for redirects?"

**No.** The redirects are already automatic and permanent; a new repo at the old name is exactly
what destroys them. GitHub docs: *"do not reuse the original name of the renamed repository. If you
do, redirects to the renamed repository will no longer work."* GitHub staff: *"new repos take
priority over redirects… the redirects will break in favor of displaying the new repo, for as long
as that new repo exists."* A pointer-repo **cannot** reproduce the service anyway: the updater hits
an API sub-resource (`/repos/thcolin/pleNx/releases/latest`); a repo does no custom HTTP redirect
there — it would just return the (empty) new pleNx repo's data, `checkUpdate` gets no `tag_name`/
`assets`, and **every installed pleNx silently loses updates forever**. A plain README-pointer at the
old name only becomes conceivable in a distant future where the whole fleet has migrated (never
100 % guaranteeable), and even then it sacrifices the remaining redirects. **Just rename; touch
nothing at `pleNx`.**

Note this is separate from the **app** identity: the console title IDs and reverse-DNS app-id are
kept **fresh** (not pleNx's), so a store-installed GMCA sits *in parallel* with an existing pleNx app
and escapes the Switch HOME-menu name/icon cache. Repo continuity (stars) and a clean parallel app
install are thus both achieved.

## How existing installs receive GMCA (the update bridge, per platform)

The bridge works, but its behaviour differs sharply by platform. Publishing GMCA `v1.0.0` on the
renamed repo is what triggers it (before that, the redirect just serves the last pleNx release, so
nothing changes for users). Version comparison is **numeric** (`sscanf` + `lexicographical_compare`,
`version.cpp:119-129`), so `1.0.0 > 0.1.10` with no string-compare trap.

| Platform (existing pleNx install) | How it gets GMCA v1.0.0 | Verdict |
|---|---|---|
| **Switch** | 301 API → GMCA release → asset matched **by `.nro` suffix** (`version.cpp:267-274`, rename-proof) → overwrites the **running NRO in place** (`argv[0]`, `version.cpp:201-205`) → working GMCA | ✅ **Automatic & transparent** |
| **Vita v0.1.11→v0.1.14** | in-app `.vpk` self-update matches `.vpk`, installs in place via `vita::installVpk` | ⚠️ Automatic **only if** the release ships a `.vpk`, **and** it lands on a GMCA that (from the stale branch) lost the Vita self-updater → regression — **B1 fixes this** |
| **Vita v0.1.10 and older** | no `.vpk` self-update → `#else` → `openBrowser` | 🟠 Semi-auto: opens the release page, **manual `.vpk` reinstall** |
| **Desktop (macOS/Win/Linux), PS4** | `#else` → `openBrowser` (`version.cpp:299`) | 🟠 **Never in-app by design**: prompt → release page → package/manual reinstall (Linux via the `gmca` package, `Replaces=plenx`) |

**Reach caveat — users who dismissed a prompt won't be re-notified automatically.** Dismissing writes
`APP_UPDATE = installed version` (`version.cpp:285,297`), and the startup check only fires when
`getVersion() != APP_UPDATE` (`main.cpp:194-195`). That population sees GMCA only via the **manual**
"check for updates" in Settings → the out-of-app announce (site/forums) is **necessary**, not cosmetic.

**Switch tile nuance.** The in-place self-update swaps the *binary* but not the HOME tile: the file
stays named `pleNx.nro` (now containing GMCA) and the forwarder tile keeps pleNx's name/icon (the
forwarder is a separate NSP keyed by the old title ID). A fresh **GMCA** tile only appears via the
other path — a parallel install from the store (fresh title ID `0104474D43410000`), with data carried
over by the migration shim. Two distinct journeys: in-place binary upgrade vs. clean parallel install.

## Blockers before tagging GMCA v1.0.0

| # | Blocker | Evidence | If ignored |
|---|---|---|---|
| **B1** | Branch cut from **v0.1.10**, behind `dev`; Vita `startUpdate`/`vita_install.*` were absent | merge-base `eb9ff00` | **✅ Resolved** — merged `origin/dev` (`a7ae506`); `vita_install.cpp` is back and globbed into the build; Vita 1080p transcode cap re-homed in `PlexBackend::resolvePlayback`. |
| **B2** | `CHANGELOG.md` has no `## [1.0.0]` section | top = `## [0.1.15]` (post-merge); guard `build.yaml:55-63` | ⛔ **Open** — `upload-release` **fails** → no release published. Add the section at release time. |
| **B3** | Flatpak manifest was still pleNx | `scripts/flatpak-manifest.yaml` | **✅ Resolved** — `app-id`/`command`/module now `fun.thcolin.gmca` / `GMCA` / `gmca`. |
| **B4** | Desktop PNG icons were still `fun.thcolin.plenx.png` | `scripts/icons/*/` | **✅ Resolved** — 5 icons renamed to `fun.thcolin.gmca.png`. |
| **B5** | Switch forwarder title ID `0104474D43410000` is an unverified placeholder | `CMakeLists.txt:57-63` (TODO) | ⛔ **Open** — possible title-ID collision; verify uniqueness (web / GitHub / GBAtemp) before the first Switch release. |

**CI fixes applied during validation** (all green except the two open blockers): Switch forwarder NSP rename now lower-cases the title id (hacbrewpack writes it lower-cased — broke once the id gained hex letters); Flatpak `glfw` fetched as a per-commit tarball and `uchardet` from the Debian mirror (freedesktop.org "go-away" 418). See PR #25.

**Non-blocking, mostly done:** site `data-version` bumped to `v1.0.0` (hero + footers) ✅; site
images committed, `site/_explore/` and `site/REDESIGN-BRIEF.md` kept out of the deploy ✅;
`share/plenx → share/gmca` ✅ (`CMakeLists.txt`); `plenx.log` / `plenx-*.dmp` → `gmca` ✅
(`main.cpp`, `misc.cpp`); `image.hpp` load() docstring de-Plex'd ✅. **Remaining:** `build-switch.sh`
still targets `pleNx.nro` (dev-only helper; CI builds `GMCA.nro`); decide the one-time
"pleNx is now GMCA" notice; decide showing the full name on the About screen.

**Intentional pleNx references to keep:** `Provides/Conflicts/Replaces=plenx` in `debian/control:18-20`
and `scripts/aur/PKGBUILD:10-12` (clean in-place upgrade); historical `CHANGELOG`/`debian/changelog`
entries; `thcolin/pleNx#1` issue refs in code comments (resolve via redirect); the `plenx::` C++
namespace (invisible); the forwarder NPDM `"name":"pleNx"` (internal — the HOME tile name comes from
`--titlename ${CMAKE_PROJECT_NAME}` = GMCA).

## The transition (runbook, ordered)

1. **✅ B1 done — `dev` integrated by merge.** `origin/dev` (v0.1.15) was merged into
   `feat/rebrand-gmca` (merge commit `a7ae506`), reconciling multi-backend × offline (#19) × music;
   the result has dev's Vita self-updater + fixes **and** the rebrand. Desktop build and the full CI
   matrix are green (nx / vita / ps4 / mingw / macos / aur / flatpak) — see PR #25.
2. **✅ B3/B4 done; ⛔ B2/B5 remain.** Flatpak manifest + the 5 icons renamed to `gmca`; site images
   committed. Still to do before the tag: add the `## [1.0.0]` CHANGELOG section (**B2**) and verify
   the Switch title-ID uniqueness (**B5**, web / GitHub / GBAtemp homebrew list).
3. **Rename** `thcolin/pleNx` → `thcolin/gamepad-media-center-aggregator` in GitHub settings
   (keeps stars). Update the local remote: `git remote set-url origin <new URL>`.
4. **Merge to `dev`** → `pages.yml` deploys the rebranded site **at the new path** (rename must
   precede this: `404.html` and `guide.html`'s canonical/og are absolute on
   `/gamepad-media-center-aggregator/`). Verify the site is live at the new path.
5. **Add `## [1.0.0]` to CHANGELOG**, commit, **tag `v1.0.0`**, push → `build.yaml` builds every
   platform and publishes a non-draft release with `GMCA-*` assets (**both `.nro` and `.vpk`**).
6. **Verify the bridge:** `curl -sIL api.github.com/repos/thcolin/pleNx/releases/latest` → 301 → GMCA
   release. Submit a **new** hb-app.store listing + a **new** VitaDB entry. Announce on the forums
   (GBAtemp, r/SwitchHomebrew, VitaDB), re-posting the new links since old Pages URLs 404.
7. **Never recreate `thcolin/pleNx`.**

## Migration mechanics — user data (verified)

- `getPackageName()` = `GMCA` (`BUILD_PACKAGE_NAME = ${PROJECT_NAME}`, `CMakeLists.txt:54,237`) names
  the per-user data folder on every platform (`sdmc:/switch/GMCA`, `ux0:/data/GMCA`,
  `~/Library/Application Support/GMCA`, XDG, `%LOCALAPPDATA%\GMCA`). `AppConfig::init()` runs a silent
  migration **before** any read: `migrateLegacyConfigDir(dataDir("pleNx"), configDir())` then
  `…("Switchlex", …)` (`config.cpp:255-256`), an `fs::rename` guarded by `exists(from) && !exists(to)`.
- **The direct jump (old pleNx or Switchlex → GMCA v1.0.0, no v0.2.0) is covered:** the same shim ships
  in v1.0.0; the guard means only the first applicable source migrates, no clobbering. The JSON schema
  is unchanged, so tokens/servers/downloads survive — **no re-login, no lost downloads**. The device
  re-appears as "GMCA" in Plex's authorized devices (token still valid, cosmetic).
- Minor reserve: `fs::rename` has no `copy` fallback; on failure (perms/EXDEV) pleNx data is left
  **orphaned but not lost** (EXDEV near-impossible — from/to are siblings in the same parent).

## Site & GitHub Pages

- Rebrand already committed; `site/assets/js/main.js:45` fetches the **new** repo directly (no 301/CORS
  involved in the browser). The `data-version` fallback still reads `v0.1.10` — bump to `v1.0.0`.
- **Old Pages URLs 404** after rename (project sites aren't redirected). Web/SEO/social only — no app
  impact. The DIY stub-redirect mitigation is **unavailable** (recreating `pleNx` would break the
  updater), and the branded `404.html` does **not** cover the old path (GitHub serves its generic 404
  for a path that maps to no repo). Real mitigation would be a **custom domain (CNAME) set before the
  rename** (GitHub's own recommendation) — not in place today.
- **Sequencing:** rename the repo **before** the Pages deploy of the rebranded site (absolute paths).

## Store notes

- **hb-app.store** keys a listing on the package `name` (libget) → a rename means a **new listing at a
  new URL**; the download counter does not carry over. Keep the old pleNx page as a pointer.
- **VitaDB** keeps a listing on a numeric id, but VitaDB-Downloader matches updates by **TITLE_ID** —
  GMCA uses a fresh `VITA_TITLEID` (`GMCA00001`), so it is a **new entry**, not an update to pleNx.
- **Flathub** (if targeted): the app-id is the key → `fun.thcolin.plenx → fun.thcolin.gmca` is a new
  submission.
- All three are independent of the in-app updater (GitHub Releases) and are submitted manually.

## Open TODOs before release

- [x] **B1** — merged `origin/dev` into the branch (`a7ae506`); Vita self-update + v0.1.11–v0.1.15
      fixes recovered, Vita 1080p cap re-homed. CI matrix green.
- [ ] **B2** — add a `## [1.0.0]` CHANGELOG section (`git cliff --unreleased --tag v1.0.0 --prepend`).
- [x] **B3** — Flatpak manifest fixed (`app-id`/`command`/module → `gmca`).
- [x] **B4** — 5 PNG icons renamed `fun.thcolin.plenx.png` → `fun.thcolin.gmca.png`.
- [ ] **B5** — verify the Switch forwarder title ID `0104474D43410000` is unique.
- [ ] **GMCA brand art** — real logo/icon and re-shot screenshots (GMCA name + PS Vita and
      Raspberry-Pi-on-a-TV imagery). Site images are committed; the art itself is still a placeholder.
- [ ] **Rename** `thcolin/pleNx` → `thcolin/gamepad-media-center-aggregator`; do **not** create
      anything at the freed `pleNx` name.
- [ ] Submit to hb-app.store (new listing) and VitaDB (new entry) after the release is published.
