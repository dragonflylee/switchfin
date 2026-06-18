# Changelog

## [0.9.1]

### Fixed

* follow framebuffer size changes when docking mid-playback
* improvements media tab layout with new UI elements

### Add

* add Turkish localization @lexicalnerd
* add series download support
* support remote server manage with add/edit form

## [0.9.0]

### Fixed

* support multi source movie
* fix zh-Hant translations @dic1911
* update Portuguese from crowdin

### Add

* add offline download support @docmeth02

## [0.8.3]

### Fixed

* add home tab retry dialog when request failed
* deviceId/deviceName generate on linux
* optimize font load on desktop platform
* json parse failed when SeriesId is null

### Add

* bonus video view for series tab 

## [0.8.2]

### Fixed

* PS4: video freeze after fast fowarding
* thumb image for episodes on home page
* add refresh support for dashboard

### Add

* option of ui scale
* i18n: add spanish localization @Nyaran

## [0.8.1]

### Fixed

* stability improvement
* adjust dashboard view

### Add

* program tab for livetv

## [0.8.0]

### Fixed

* webdav client for clouddrive2
* refactor home tab for split media

### Add

* dashboard tab for view devices, activities etc.
* video profile add subtitle codec

## [0.7.7]

### Fixed

* switch: compatibility update for 21.0.0
* adjust direct play logic

### Add

* context menu for tag favorite

## [0.7.6]

### Fixed

* **login issue on jellyfin 10.11**
* switch: tls1.3 support by mbedtls
* ps4: av1 direct playback

## [0.7.5]

### Add

* add song/artist tab for music
* add keyboard action binding config
* support receive media files as args
* i18n: add russian localization @wildrun0

### Fixed

* ps4: image load thread config
* ps4: 5.1 audio is in slow motion

## [0.7.4]

### Fixed

* LiveTV not working

## [0.7.2]

### Fixed

* audio/sub selection when transcode
* vita: compress image texture
* resume play on movie tab
* linux/macos fixed zero-copy hwdec

## [0.7.1]

### Add

* add suggest/genres tab for collection

### Fixed

* series/movie view logo layout
* refactor media collection view
* image load for search sugguest 

## [0.7.0]

### Add

* refactor series view layout
* add people view of series/movie
* add Brazilian Portuguese localization (@brunorossetto)
* external drive support using [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs)

### Fixed

* exception catch under macOS
* remote load large file list
* use mpv watch-later for remote
* subtitle not display on vita

## [0.6.3]

### Fixed

* websocket close gracefully
* vita transcode audio codec
* upgrade ffmpeg 7.1.1

### Add

* player option for video rotate
* photo viewer support

## [0.6.2]

### Fixed

* strm format support
* search suggest include `MusicAlbum`
* improve image load speed
* optimize auto quality option

### Add

* **add mirror play support**
* add korean localization (@KimPig)
* add psvita gxm support
* custom vsync interval

## [0.6.1]

### Fixed

* ignore save resume on m3u playlist
* ignore save window stat when fullscreen
* websocket support on switch

## [0.6.0]

### Add

* add japanese localization (@yyoossk)
* add vietnamese localization (@tlover-code)
* support modify server url
* remote add ftp/sftp support
* remote add resume history

### Fixed

* loading osd when change episode
* update server info when check login
* audio load failed when proxy enabled
* navigate route of video view
* focus logic of lock button

## [0.5.3]

### Add

* save windows state when exit
* support touch input on linux (@xfangfang)
* support paste text to search input (@xfangfang)
* new option low quality
* new option subtitle fallback
* support custom user-agent in webdav browser
* http server support for remote

### Fixed

* support loading custom emoji font
* empty tab when search failed

## [0.5.2]

### Fixed

* disable update notify when user caneled
* decode danmaku failed
* filter missing when sync option off
* auto selected external subtitles

## [0.5.1]

### Add

* upgrade ffmpeg 7.0.1
* add highlight for current play track
* **add live tv support**
* add webdav file browser
* add option for swap abxy buttons (desktop only)

### Fixed

* network timeout for load video
* panic after cancel login
* abnormal focus when login after add server
* server list url display

## [0.5.0]

### Added

* [danmu plugin](https://github.com/cxfksword/jellyfin-plugin-danmu) integration
* video flip and subtitle sync support for player
* video equalizer setting
* add episode selector on player view
* add new option `Always on top` for desktop platform
* add Ukrainian localization (@skylegend509)
* add Czech localization (@miraficus)

### Fixed

* support `MusicView` and `PlayList`
* forward nsp add startup logo

## [0.4.5]

### Added

* add chapter point on progress bar
* touch gesture for volume/brightness

### Fixed

* collection display on new created user
* Scroll performance improvement @xfangfang

## [0.4.4]

### Added

* http proxy option for request
* sync filter setting with official media player
* setting entry from server add tab
* add more viewd badge

### Fixed

* osd lock touch gesture
* media filter of collection tab
* wrong toggle icon after seek when paused

## [0.4.3]

### Added

* add logout from setting tab
* player view add touch gesture
* upgrade ffmpeg 6.1.1

### Fixed

* series tab: viewd badge
* device name under macOS

## [0.4.2]

### Added

* add close button for series/collection
* add lock button for player view
* add close button for player view

### Fixed

* auto hide osd on loading end
* fix login disclaimer after switch server
* fix cannot trigger loading next page after refresh

## [0.4.1]

### Added

* add tap control for progress slider
* add volume control for video
* add login disclaimer display
* add play progress on more page

### Fixed

* server selection by joystick
* fix home tab auto refresh

## [0.4.0]

### Added

* **add music album support**
* add option of player_inmemory_cache
* add experimental support for deko3d backend

### Changed

* save video quality to config
* improve password input
* update ffmpeg to 6.1

### Fixed

* option for speed control
* osd show timeout

## [0.3.4]

### Added

* add option for open debug layer
* add support of clear search history 

### Fixed

* improve url check for server add
* fix osd navigate with dpad/analog stick

## [0.3.3]

### Added

* movie collection support
* add filter for media collection
* add speed up feature when pressed on
* add play/stop action when double click
* ci: add d3d11 backend for win32

### Fixed

* fix server list focus route

## [0.3.2]

### Added

* add new option forceDirectPlay
* support chapter select
* support subtitle/audio track selection on transcode play
* ci: add mingw64 workflow

### Fixed

* fix trancode codec type
* fix crash on exit when playing media
* switch: sub fallback font

## [0.3.1]

### Added

* switch: FFmpeg: support vp9 hwdec
* server list support user selection
* login quick connect support

### Fixed

* crash when http exception
* flac audio direct play

## [0.3.0]

### Added

* **switch: FFmpeg: Update to 6.0 with hwaccel backend**, Thanks to **averne**
* set mpv option `subs-fallback` to `yes`
* complete server list selector

### Fixed

* search result not update when delete charactor
* fix: setting selection which required restart
* fix searies tab layout
