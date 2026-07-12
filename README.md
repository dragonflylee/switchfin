<div align="center">

<img src="site/assets/img/gmca-logo.webp" alt="GMCA" width="132">

# GMCA — Gamepad Media Center Aggregator

*/ˌdʒiː.ɛm.siː.ˈeɪ/ — Gamepad Media Center Aggregator*

**🎮 📺 — every media server, on the screens you forgot.**

GMCA is one native, controller-first **client**. Point it at any **media center** —
**Plex, Jellyfin, Emby or Stremio** — and run it on any **device** you left in a drawer:
**Nintendo Switch**, **PS Vita** or a **Raspberry Pi wired to the TV**. The interface never
changes; only the accent colour and the tabs follow your server. Also runs on Windows, macOS, Linux and PS4.

[![build](https://github.com/thcolin/gamepad-media-center-aggregator/actions/workflows/build.yaml/badge.svg)](https://github.com/thcolin/gamepad-media-center-aggregator/actions/workflows/build.yaml)
[![NS](https://img.shields.io/badge/-Nintendo%20Switch-e4000f?style=flat&logo=Nintendo%20Switch)](https://github.com/thcolin/gamepad-media-center-aggregator/releases/latest)
[![PSVita](https://img.shields.io/badge/-PSVita-003791?style=flat&logo=PlayStation)](https://github.com/thcolin/gamepad-media-center-aggregator/releases/latest)
[![download](https://img.shields.io/github/downloads/thcolin/gamepad-media-center-aggregator/total?label=downloads)](https://github.com/thcolin/gamepad-media-center-aggregator/releases/latest)
[![nightly](https://img.shields.io/badge/nightly-build-green)](https://nightly.link/thcolin/gamepad-media-center-aggregator/workflows/build.yaml/dev)
[![sponsor](https://img.shields.io/badge/sponsor-%E2%9D%A4-db61a2)](https://github.com/sponsors/thcolin)

</div>

> [!NOTE]
> GMCA is the multi-backend evolution of **pleNx** (a native Plex client), itself a fork of
> [Switchfin](https://github.com/dragonflylee/switchfin) — the Nintendo Switch Jellyfin client.
> Existing pleNx installs migrate their data to GMCA automatically on first launch. It is an
> independent, third-party project, not affiliated with or endorsed by Plex, Jellyfin, Emby or Stremio.

---

## Screenshots

One interface across every source. The same layout serves a Plex library, a Jellyfin series,
an Emby film and a Stremio catalogue — only the accent and the tabs change.

**The same Home, whatever you run**

<div align="center">

| Plex | Jellyfin | Emby | Stremio |
|:---:|:---:|:---:|:---:|
| ![Home on Plex](site/assets/img/gallery/scr-home-plex.webp) | ![Home on Jellyfin](site/assets/img/gallery/scr-home-jellyfin.webp) | ![Home on Emby](site/assets/img/gallery/scr-home-emby.webp) | ![Home on Stremio](site/assets/img/gallery/scr-home-stremio.webp) |

</div>

**Every screen, built for a gamepad**

<div align="center">

| Movie detail | Series | Season |
|:---:|:---:|:---:|
| ![Movie detail](site/assets/img/gallery/scr-movie-plex.webp) | ![Series](site/assets/img/gallery/scr-series-jellyfin.webp) | ![Season](site/assets/img/gallery/scr-season-emby.webp) |
| **Search** | **Genres** | **Quick actions** |
| ![Search](site/assets/img/gallery/scr-search-stremio.webp) | ![Genres](site/assets/img/gallery/scr-genres-plex.webp) | ![Quick actions](site/assets/img/gallery/scr-quickactions-jellyfin.webp) |
| **Offline downloads** | **Person page** | **Connection switcher** |
| ![Downloads](site/assets/img/gallery/scr-downloads-emby.webp) | ![Person page](site/assets/img/gallery/plex-person.webp) | ![Connection switcher](site/assets/img/gallery/switcher.webp) |

</div>

> More shots — and a live gallery you can flip between servers — on the
> [GMCA site](https://thcolin.github.io/gamepad-media-center-aggregator/#screens).

## Backends — one app, every server

- **Plex** — sign in with a 4-character code on [plex.tv/link](https://plex.tv/link), pick your
  server and your Plex Home profile (PIN-protected profiles supported). Watchlist, hubs, universal transcode.
- **Jellyfin / Emby** — add your server URL, then Quick Connect (no keyboard) or username & password.
  Favorites, resume, Next Up, transcoding — no third-party account required.
- **Stremio** — log in to your account; your addon catalogs and library sync across. Direct links and
  debrid streams play straight through.
- **File servers** — no media server? Browse and play from **WebDAV, FTP, SFTP and HTTP(S)** shares,
  added straight from the UI with a connection test. USB drives too, on Switch.

The tabs are **capability-aware**: Watchlist, Favorites and Collections appear only on the servers that
have them. Keep every server and profile side by side and hop between them from one avatar — the
**connection switcher** never asks you to log in twice.

## Devices — built for the screens you forgot

- **Nintendo Switch** — native NRO on Atmosphère CFW; HOME-menu tile for full-memory playback;
  external USB drives via [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs).
- **PS Vita** — a first-class VPK for jailbroken consoles (on [VitaDB](https://www.rinnegatamante.eu/vitadb/));
  that OLED finally earns its keep again.
- **Raspberry Pi on the TV** — a Flatpak (x86_64 / arm64v8) and an Arch package; a Pi wired to the
  television plus any controller becomes a couch-ready media box.
- **Desktop** — Windows, macOS and Linux builds; plug in a pad or drive it all from the keyboard.

## Features

- **Sign in without a keyboard** — code-based sign-in, Quick Connect and PIN profiles.
- **Home mirrors your server** — Continue Watching, then every hub your server exposes, in its order.
- **Your personal list** — Plex Watchlist, Jellyfin/Emby Favorites or the Stremio library, in the sidebar
  and on every detail page.
- **Rich detail pages** — full-bleed backdrop with title logo, cast with full **person pages**, related rows.
- **Season pages** — artwork, episode count and synopsis, with one-tap **full-season download**.
- **Quick actions on any poster** — press <kbd>X</kbd> (or long-press): go to show/season, mark watched, download.
- **Per-server theming** — the accent follows the source: Plex amber, Jellyfin cyan, Emby green, Stremio violet.
- **Connection switcher** — every server and account side by side; switch from one avatar, no re-login.
- **Playback with MPV** — direct play and transcode (HLS), resume, chapters, external/embedded subtitles, audio tracks.
- **Offline downloads** for playback without a connection (original quality).
- **Native & fast** — compiled per platform, no Electron and no browser tab, even on nine-year-old silicon.
- **Available in 14 languages.**

> [!TIP]
> MPV decodes H.264, H.265, VP8, VP9 and AV1 video; Opus, FLAC, MP3, AAC, AC-3, E-AC-3,
> TrueHD and DTS audio; and SRT, VTT, SSA/ASS and DVDSUB subtitles.

## Install

Builds for every platform are attached to each
[release](https://github.com/thcolin/gamepad-media-center-aggregator/releases/latest), with nightly
artifacts on [nightly.link](https://nightly.link/thcolin/gamepad-media-center-aggregator/workflows/build.yaml/dev).

**Nintendo Switch**

1. Copy `GMCA.nro` to `sdmc:/switch/` and launch it from the homebrew menu.
2. On first launch in **applet mode**, GMCA offers to install a HOME menu tile: press the button,
   confirm, and the app relaunches as a regular title with full memory (required for video playback).
   Alternatively, hold <kbd>R</kbd> while launching any game (title takeover).
3. Connect a Plex, Jellyfin, Emby or Stremio server — or a file share — and enjoy.

> [!IMPORTANT]
> Video playback needs full-memory mode. Applet mode is fine for browsing, but install
> the HOME tile (or use title takeover) before starting a movie.

**PS Vita** — install `GMCA.vpk` with VitaShell, or find GMCA on VitaDB. **Raspberry Pi / desktop** —
grab the Flatpak or the desktop package from the latest release.

## Controls during playback

| Gamepad | Keyboard | Action |
|:---:|:---:|---|
| A | `space` | Play / Pause |
| B | `esc` | Stop |
| Y | `o` | Toggle OSD |
| X | `f4` | Menu |
| R / L | `]` / `[` | Seek forward / back |
| + | `f1` | Video profile |
| R stick | `f2` | Video quality |
| L stick | `f3` | Playback speed |

## System requirements

- **Nintendo Switch** with Atmosphère CFW (full-memory mode for playback)
- **PS Vita** with HENkaku / h-encore
- **Raspberry Pi / Linux** Flatpak (x86_64 / arm64v8) with OpenGL 3 support
- **Windows** 7 or later with DirectX 11.1 support
- **macOS** 10.15 or later (Intel or Apple Silicon)

## FAQ

**Coming from pleNx?**
GMCA reads your existing library, logins and downloads automatically on first launch — nothing to re-configure.

**Subtitles don't show up on Switch?**
Drop any TrueType font at `sdmc:/switch/GMCA/subfont.ttf`.

**macOS won't open the app?**
Clear the quarantine attribute:

```shell
sudo xattr -rd com.apple.quarantine /Applications/GMCA.app
```

**How do I enable an external drive on Switch?**
Set `ums` in `config.json` (in `sdmc:/switch/GMCA/`):

```json
{
  "setting": {
    "ums": true
  }
}
```

## Building from source

GMCA is C++17, built on [borealis](https://github.com/natinusala/borealis) for the UI
and [mpv](https://github.com/mpv-player/mpv) for playback.

```shell
git clone https://github.com/thcolin/gamepad-media-center-aggregator.git --recurse-submodules --shallow-submodules
```

The borealis submodule carries local fixes applied at build time — CI runs
`git -C library/borealis apply scripts/patches/borealis-fixes.patch` before building.

### Nintendo Switch

All-in-one Docker script (mirrors the CI):

```shell
./scripts/build-switch.sh                  # deko3d driver (recommended)
DRIVER=opengl ./scripts/build-switch.sh    # OpenGL fallback
```

Or with a local [devkitPro](https://devkitpro.org/wiki/Getting_Started) toolchain:

```shell
sudo dkp-pacman -S switch-dev switch-glfw switch-libwebp switch-curl switch-libmpv
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch GMCA.nro -j$(nproc)
```

### Desktop (macOS / Linux)

```shell
cmake -B build_desktop -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build_desktop
```

### Windows (MinGW64)

```shell
pacman -S ${MINGW_PACKAGE_PREFIX}-cc ${MINGW_PACKAGE_PREFIX}-ninja ${MINGW_PACKAGE_PREFIX}-cmake
cmake -B build_mingw -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build_mingw
```

The multi-backend architecture is documented in [MULTI_BACKEND.md](MULTI_BACKEND.md); the
pleNx → GMCA transition in [GMCA_MIGRATION.md](GMCA_MIGRATION.md).

## Support

If GMCA is useful to you, consider [sponsoring the project](https://github.com/sponsors/thcolin) —
it directly funds development time and devices to test on.

## Acknowledgements

GMCA stands on the shoulders of the homebrew and open-source community:

- **[@dragonflylee](https://github.com/dragonflylee)** for [Switchfin](https://github.com/dragonflylee/switchfin), the Jellyfin client this is forked from
- **[@xfangfang](https://github.com/xfangfang)** for [wiliwili](https://github.com/xfangfang/wiliwili)
- [@natinusala](https://github.com/natinusala) and XITRIX for [borealis](https://github.com/natinusala/borealis)
- [@devkitPro](https://github.com/devkitPro) and switchbrew for [libnx](https://github.com/switchbrew/libnx)
- [@proconsule](https://github.com/proconsule) for [nxmp](https://github.com/proconsule/nxmp)
- [@averne](https://github.com/averne) for the [FFmpeg](https://github.com/averne/FFmpeg) hwaccel backend and the deko3d backend of mpv
