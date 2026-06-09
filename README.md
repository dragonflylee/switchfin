# Switchlex

<img src="scripts/switchlex.svg" alt="icon" height="128" width="128" align="left">

Switchlex is a third-party **Plex** client that provides a native user interface to browse and play movies and series, primarily targeting the Nintendo Switch (also builds for Windows/macOS/Linux/PS4/PSVita/Android).
<br>

[![build](https://github.com/thcolin/switchlex/actions/workflows/build.yaml/badge.svg)](https://github.com/thcolin/switchlex/actions/workflows/build.yaml)
[![download](https://img.shields.io/github/downloads/thcolin/switchlex/total?label=Downloads)](https://github.com/thcolin/switchlex/releases/latest)
[![nightly](https://img.shields.io/badge/nightly-build-green)](https://nightly.link/thcolin/switchlex/workflows/build.yaml/dev)

> Switchlex is a fork of [Switchfin](https://github.com/dragonflylee/switchfin) (a Jellyfin client)
> migrated to the Plex API. The migration plan and full Jellyfin↔Plex correspondence tables live in
> [PLEX_MIGRATION.md](PLEX_MIGRATION.md). **The Plex port (phases 0-5) has not yet been validated
> against a real server.**

**This project is in its early stages so expect bugs.**

## Features

- Completely native interface
- Supported media items: movies, series, seasons, episodes
  - Direct play and transcoding
- Download for offline playback
- Remote browser for Webdav/Apache/Nginx/FTP server
- Based on MPV Player
  - Container formats: mkv, mov, mp4, avi
  - Video codecs: H.264, H.265, VP8, VP9, AV1
  - Audio codecs: Opus, FLAC, MP3, AAC, AC-3, E-AC-3, TrueHD, DTS, DTS-HD
  - Subtitle codecs: SRT, VTT, SSA/ASS, DVDSUB
  - Optionally force software decoding when hardware decoding has issues.
- External drive support using [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs)

## Input mapping during playback

gamepad | keyboard | describe
---|-------|---------
 A | space | Play/Pause
 B | esc | Stop during
 Y | o | Toggle OSD
 X | f4 | Show Menu
 R/L | [/] | Seek +/-
 \+ | f1 | Show video profile
 R | f2 | Stick Button Toggle Video Quality
 L | f3 | Stick Button Toggle Speed Select

## System Requirements

* Windows 7 or later with DirectX 11.1 support
* Intel or Apple Silicon Mac models 10.15 or later
* Linux flatpak x86_64/arm64v8 with OpenGL3 support

## FAQ

1. Q: Subtitles didn't display?
   A: Put any ttf file at `/switch/Switchlex/subfont.ttf`
2. Q: How to enable external drive on switch?
   A: Edit config file `config.json`

```json
{
  "setting": {
    "ums": true
  }
}
```

3. Q: How to play media files on webdav server?
   A: Edit config file `config.json`

```json
{
  "remotes": [
    {
      "name": "local",
      "url": "file:///switch"
    },
    {
      "name": "xiaoya",
      "passwd": "guest_Api789",
      "url": "webdav://192.168.1.5:5678/dav",
      "user": "guest"
    },
    {
      "name": "rpi",
      "url": "sftp://pi:raspberry@192.168.1.5/media"
    },
    {
      "name": "rclone",
      "url": "http://192.168.1.5:8000"
    }
  ]
}
```

* example for using [rClone](https://rclone.org/downloads/) setup HTTP server

```bash
rclone serve http --addr :8000 --read-only /media/downloads
```

4. Q: Can't open app under macOS ?
   A: Please run this command in your terminal: `sudo xattr -rd com.apple.quarantine /Applications/Switchlex.app`

## Roadmap (Plex migration)

See [PLEX_MIGRATION.md](PLEX_MIGRATION.md) for details.

- [x] Phase 0 — strip Jellyfin-only features (danmaku, admin dashboard, Live TV, music, favorites, remote control), rebrand
- [x] Phase 1 — Plex API foundation (X-Plex headers, PIN auth, plex.tv resources, models)
- [x] Phase 2 — server/profile connection UI
- [x] Phase 3 — browsing (hubs, libraries, detail pages, search)
- [x] Phase 4 — playback (direct play, universal transcode HLS, timeline/scrobble)
- [x] Phase 5 — collections, downloads (offline playback), photos
- [ ] Validation against a real Plex Media Server + Switch (devkitPro) build
- [ ] Phase 6 — extras (intro markers, BIF trickplay, watchlist, sessions)

## Develop

```shell
git clone https://github.com/thcolin/switchlex.git --recurse-submodules --shallow-submodules
```

### Building for Switch

To build for Switch, a standard development environment must first be set up. In order to do so, [refer to the Getting Started guide](https://devkitpro.org/wiki/Getting_Started).

```bash
sudo dkp-pacman -S switch-dev switch-glfw switch-libwebp switch-curl switch-libmpv
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch Switchlex.nro -j$(nproc)
# for debug
nxlink -a <YOUR IP> -p Switchlex/Switchlex.nro -s Switchlex.nro --args -d -v
```

### Building for MinGW64

```bash
pacman -S ${MINGW_PACKAGE_PREFIX}-cc ${MINGW_PACKAGE_PREFIX}-ninja ${MINGW_PACKAGE_PREFIX}-cmake
cmake -B build_mingw -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build_mingw
```

## Thanks to

- **@dragonflylee for [Switchfin](https://github.com/dragonflylee/switchfin), the Jellyfin client this project is forked from**
- **@xfangfang for [wiliwili](https://github.com/xfangfang/wiliwili)**
- @devkitpro and switchbrew for [libnx](https://github.com/switchbrew/libnx)
- @natinusala and XITRIX for [borealis](https://github.com/natinusala/borealis)
- @proconsule for [nxmp](https://github.com/proconsule/nxmp)
- @averne for great work of [FFmpeg](https://github.com/averne/FFmpeg) hwaccel backend
- @averne deko3d backend of [mpv](https://github.com/averne/mpv)
