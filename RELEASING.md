# Releasing pleNx

Checklist for cutting a new version. The release is fully driven by pushing a
`vX.Y.Z` tag: the [`build`](.github/workflows/build.yaml) workflow builds every
platform and then the `upload-release` job publishes a **non-draft** GitHub
release. The in-app updater reads `releases/latest` and ignores drafts, so the
release goes live for users the moment that job finishes.

Two guard rails run before any release is published, so getting them wrong
fails the build instead of shipping a broken release:

- the tag must equal the `CMakeLists.txt` version (`MAJOR.MINOR.ALTER`);
- `CHANGELOG.md` must contain a `## [X.Y.Z]` section (its body becomes the
  release notes).

## Steps

1. **CHANGELOG.md** — add a `## [X.Y.Z] - YYYY-MM-DD` section at the top
   (newest first). Generate a draft with
   `git cliff --unreleased --tag vX.Y.Z --prepend CHANGELOG.md`, then curate by
   hand.

2. **CMakeLists.txt** — bump `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_ALTER`
   (the project info block, ~line 55). These three concatenated must match the
   tag. If you ship the PS Vita build, also bump `VITA_VERSION` (2-digit
   `MM.NN` format, separate from the main version — not enforced by the
   workflow).

3. **Promo site** — bump the static `data-version` fallback to `vX.Y.Z` in all
   three pages. `site/assets/js/main.js` overwrites it at runtime by fetching
   `releases/latest`, but the fallback is what shows when the API is rate-
   limited/unreachable or before the release is published, so keep it in sync:
   - `site/index.html` (two occurrences: hero link + footer)
   - `site/guide.html` (footer)
   - `site/404.html` (footer)

4. **Commit** the steps above as `chore(release): vX.Y.Z`. Keep unrelated
   changes (e.g. the `library/borealis` submodule) out of this commit.

5. **Tag and push**:
   ```sh
   git tag vX.Y.Z
   git push origin dev
   git push origin vX.Y.Z
   ```

6. **Watch the build** — the tag run is the one whose ref is `vX.Y.Z`. When
   `upload-release` succeeds, the GitHub release is published and the AUR
   `pkgver` is derived automatically from the tag. Nothing else to do.

## Store listings

pleNx is listed on the two console homebrew stores. Users who installed through
a store expect updates from it, so keep the listings in sync — otherwise they
get stuck on (or are offered) an old version.

- **Switch — Homebrew App Store (ForTheUsers).** Fully automated: ForTheUsers'
  `update-checker` bot watches this repo's GitHub releases and opens a PR to
  [`fortheusers/switch-hbas-repo`](https://github.com/fortheusers/switch-hbas-repo)
  (`packages/pleNx/pkgbuild.json`) for every new tag. Nothing to build on our
  side — the PR just has to be merged by ForTheUsers. If the store lags, check
  the open `[auto] Update pleNx to X.Y.Z` PRs there and ping them to merge.

- **Vita — VitaDB.** Not automatable: VitaDB updates are moderator/role-gated
  (no self-service API). pleNx is entry **#1411**. After a release, update that
  entry to the new version so its daemon does not offer VitaDB users an outdated
  build (the "downgrade" prompt reported in issue #14). Best long-term fix: ask
  the VitaDB maintainer to point the entry's `url` at the stable permalink
  `https://github.com/thcolin/pleNx/releases/latest/download/pleNx.vpk`, which
  always resolves to the newest asset. Note that pleNx also self-updates in-app
  on Vita since v0.1.14, so users who stay on that channel are unaffected.

## Notes

- The default/main branch is `dev`.
- The promo site (`site/`) deploys separately via
  [`pages.yml`](.github/workflows/pages.yml) on push to `dev`; it does not need
  the tag. Pushing the release commit to `dev` already refreshes it.
