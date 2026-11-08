# AGENTS.md

Switchfin is a native C++17 Jellyfin client built on [borealis](https://github.com/xfangfang/borealis)
(`library/borealis` submodule). Targets Switch, PS Vita, PS4, Windows, macOS, Linux, Android. Playback via libmpv.

## Build & run

See [README "Development"](README.md#development) for prerequisites.

- CMake requires **one platform flag**: `-DPLATFORM_DESKTOP=ON` or `-DPLATFORM_SWITCH=ON`.
- Desktop: `cmake -B build -DPLATFORM_DESKTOP=ON && cmake --build build` (`build/` already exists).
- Windows (MinGW + D3D11): `cmake -B build -G 'MinGW Makefiles' -DPLATFORM_DESKTOP=ON -DUSE_D3D11=ON -DUSE_SYSTEM_GLFW=ON && cmake --build build -j$(nproc)`.
  `USE_D3D11` = D3D11 instead of OpenGL; `USE_SYSTEM_GLFW` = use installed glfw3 instead of FetchContent.
- Switch: `cmake -B build_switch -DPLATFORM_SWITCH=ON && make -C build_switch Switchfin.nro -j$(nproc)`.
- Optional features: `USE_MUPDF`, `USE_WEBP`, `USE_BOOST_FILESYSTEM`, `USE_LIBROMFS`, `MPV_SW_RENDER`, `BUILTIN_NSP`, `USE_LIBUSBHSFS`.
- `BUILTIN_NSP` uses hacBrewPack (build-time) to generate `resources/forwarder.nsp` into romfs; `app/src/utils/forwarder.cpp` splits its NCAs and registers them into NCM at runtime (no `nspmini`).
- **No unit tests.** Verify by compiling the target and running the app.
- CI: `.github/workflows/build.yaml`; packaging: [scripts/](scripts/README.md).
- Formatting: `.clang-format` (4-space, column 120, `SortIncludes: Never` — don't reorder includes).

## Architecture

`app/include/<dir>` and `app/src/<dir>` mirror each other:

| Dir | Responsibility |
|-----|----------------|
| `activity/` | Full-screen `brls::Activity` pages (boot, server list, main tab host, player). |
| `api/` | HTTP + Jellyfin REST: `HTTP`, `websocket`, `analytics`, `jellyfin/` (endpoints + JSON DTOs). |
| `client/` | Non-Jellyfin backends: WebDAV, Apache dir index, local FS, AVIO; entry `remote::create()`. |
| `tab/` | Tab content (`AttachedView` + `Presenter`): `HomeTab`, `SettingTab`, media detail pages. |
| `view/` | Reusable borealis widgets: `VideoView`, `RecylingVideo`, `SVGImage`, `RecyclingGrid`, `MPVCore`. |
| `utils/` | Singletons/helpers: `AppConfig`, `ThreadPool`, downloads, keybinds, events, version. |

- Entry point `app/src/main.cpp`: `AppConfig::instance().init()` → `brls::Application::init()` → `registerXMLView(...)` for every XML view → `while (brls::Application::mainLoop());`.
- Base URL + token from `AppConfig` (`getUrl()`, `getToken()`); auth header built in `AppConfig::getAuth()` (`utils/config.cpp`).

## Adding UI

1. Derive `brls::Activity` (full screen), `AttachedView` + `Presenter` (tab), or `brls::Box` (widget).
2. Inflate: `CONTENT_FROM_XML_RES("activity/x.xml")` or `inflateFromXMLRes("xml/tabs/x.xml")`.
3. Bind members with `BRLS_BIND(Type, member, "path/id")`.
4. Add `static brls::View* create();` and **register it in `main.cpp`**.
5. XML in `resources/xml/{activity,tabs,view}/`: `@res/...` assets, `@theme/...` colors.

## Async & threading rules

- Never block the UI thread: network work via `brls::async(...)`, results back via `brls::sync(...)`.
- **Guard view lifetime**: `ASYNC_RETAIN` before launch, capture `[ASYNC_TOKEN]`, `ASYNC_RELEASE` at the top of **both** success and error callbacks. Counts must match.
- Prefer `jellyfin::getJSON/postJSON/deleteJSON` (`api/jellyfin.hpp`) — they already do async + parse + `brls::sync`.
- Report errors via `OnError` (usually a `brls::Dialog`); log with `brls::Logger::error("...{}", ...)`.

## Conventions

- Files `snake_case.hpp/.cpp`; classes `CamelCase` + `Activity`/`Tab`/`View` suffix.
- `#pragma once`; keep existing `/* Copyright ... */` headers.
- Namespaces: global app classes; `jellyfin` (API), `remote` (clients). Use `using namespace brls::literals;`.
- 4-space indent, Allman braces, `Type* ptr`, `this->` for members, `override`, C++17.
- Preserve intentional typos: `RecylingVideo` (`view/recyling_video.hpp`), `setTitie` (`view/video_view.hpp`).

## Internationalization

- Use `_i18n` with namespaced keys, e.g. `"home/resume"_i18n`.
- Files: `resources/i18n/<locale>/main.json` (+ `hints.json`); nested key `a.b` → `"a/b"`.
- Add new strings to **all** locales (at least `en-US`).

## Pitfalls

- Heavy `#ifdef`: `__SWITCH__`, `__PS4__`, `__PSV__`, `ANDROID`, `_WIN32`, `__APPLE__`, `BOREALIS_USE_STD_THREAD`, `USE_WEBP`. Ensure every target compiles.
- Don't edit `library/` (submodules: borealis, lunasvg) unless the task requires it.
- `AppConfig : brls::Singleton<AppConfig>` — access via `AppConfig::instance()`, persisted as JSON.
