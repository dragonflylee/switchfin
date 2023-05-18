# Switchfin

Switch/PC平台 Jellyfin 客户端

```bash
git submodule update --init --recursive --depth 1
```

## Building for Switch

To build for Switch, a standard development environment must first be set up. In order to do so, [refer to the Getting Started guide](https://devkitpro.org/wiki/Getting_Started).

```bash
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch Switchfin.nro -j$(nproc)
```

## Building for MinGW64

```bash
# https://github.com/niXman/mingw-builds-binaries/releases/download/12.2.0-rt_v10-rev2/x86_64-12.2.0-release-posix-seh-msvcrt-rt_v10-rev2.7z
# https://curl.se/windows/dl-8.1.0_1/curl-8.1.0_1-win64-mingw.zip
cmake -B build-pc -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release -DWIN32_TERMINAL=ON -G "MinGW Makefiles"
mingw32-make -C build-pc -j$(nproc)
```

## Lanuch Jellyfin Server

```bash
docker run --restart always --name jf1 --tmpfs /cache -v /media:/media \
  --device /dev/dri/renderD128:/dev/dri/renderD128 --device /dev/dri/card0:/dev/dri/card0 \
  -l 'traefik.http.routers.jf1.rule=Host(`jf1.domain.com`)' \
  -l 'traefik.http.services.jf1.loadbalancer.server.port=8096' \
  -l 'traefik.enable=true' --network kind -m 1GB -d jellyfin/jellyfin
```