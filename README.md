# Switchfin

<img src="scripts/switchfin.svg" alt="icon" height="128" width="128" align="left">

Switchfin is third-party PC player for Jellyfin that provides a native user interface to browse and play movies and series.
<br>

[![build](https://github.com/dragonflylee/switchfin/actions/workflows/build.yaml/badge.svg)](https://github.com/dragonflylee/switchfin/actions/workflows/build.yaml)
[![NS](https://img.shields.io/badge/-Nintendo%20Switch-e4000f?style=flat&logo=Nintendo%20Switch)](https://hb-app.store/switch/Switchfin)
[![PSVita](https://img.shields.io/badge/-PSVita-003791?style=flat&logo=PlayStation)](https://www.rinnegatamante.eu/vitadb/#/info/1258)
[![PS4](https://img.shields.io/badge/-PS4-003791?style=flat&logo=PlayStation)](https://pkg-zone.com/details/SFIN00000)
[![Flathub](https://img.shields.io/flathub/v/fun.dragonfly.switchfin)](https://flathub.org/apps/fun.dragonfly.switchfin)
[![download](https://img.shields.io/github/downloads/dragonflylee/switchfin/total?label=Downloads)](https://github.com/dragonflylee/switchfin/releases/latest)
[![nightly](https://img.shields.io/badge/nightly-build-green)](https://nightly.link/dragonflylee/switchfin/workflows/build.yaml/dev)

**This project is in its early stages so expect bugs.**

## Screenshots

<table>
  <tbody>
    <tr>
      <th>Home</th>
      <th>Library</th>
    </tr>
    <tr>
      <td><img src="images/home.jpg" alt="Home"></td>
      <td><img src="images/library.jpg" alt="Library"></td>
    </tr>
    <tr>
      <th>Search</th>
      <th>Music</th>
    </tr>
    <tr>
      <td><img src="images/search.jpg" alt="Search"></td>
      <td><img src="images/music.jpg" alt="Music"></td>
    </tr>
    <tr>
      <th>Series</th>
      <th>Episode</th>
    </tr>
    <tr>
      <td><img src="images/series.jpg" alt="Series"></td>
      <td><img src="images/episode.jpg" alt="Episode"></td>
    </tr>
  </tbody>
</table>

## Features
- Completely native interface
- Supported media items: movies, series, seasons, episodes 
  - Direct play and transcoding
- Remote browser for Webdav/Apache/Nginx/FTP server
- Base on MPV Player
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
   A: Put any ttf file at `/switch/Switchfin/subfont.ttf`

2. Q: How to play media files on webdav server?
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

3. Q: Can't open app under macOS ?
   A: Please run this command in your terminal: `sudo xattr -rd com.apple.quarantine /Applications/Switchfin.app`

## TODO list

- [x] Movie view
- [x] Series detail
- [x] Search page
- [x] Websocket connection (MirrorPlay)
- [x] [danmu plugin](https://github.com/cxfksword/jellyfin-plugin-danmu) integration

## Develop

```shell
git clone https://github.com/dragonflylee/switchfin.git --recurse-submodules --shallow-submodules
```

### Building for Switch

To build for Switch, a standard development environment must first be set up. In order to do so, [refer to the Getting Started guide](https://devkitpro.org/wiki/Getting_Started).

```bash
sudo dkp-pacman -S switch-dev switch-glfw switch-libwebp switch-curl switch-libmpv
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch Switchfin.nro -j$(nproc)
# for debug
nxlink -a <YOUR IP> -p Switchfin/Switchfin.nro -s Switchfin.nro --args -d -v
```

### Building for MinGW64

```bash
pacman -S ${MINGW_PACKAGE_PREFIX}-cc ${MINGW_PACKAGE_PREFIX}-ninja ${MINGW_PACKAGE_PREFIX}-cmake
cmake -B build_mingw -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build_mingw
```

## Thanks to

- **@xfangfang for [wiliwili](https://github.com/xfangfang/wiliwili)**
- @devkitpro and switchbrew for [libnx](https://github.com/switchbrew/libnx)
- @natinusala and XITRIX for [borealis](https://github.com/natinusala/borealis)
- @proconsule for [nxmp](https://github.com/proconsule/nxmp)
- @averne for great work of [FFmpeg](https://github.com/averne/FFmpeg) hwaccel backend
- @averne deko3d backend of [mpv](https://github.com/averne/mpv)
