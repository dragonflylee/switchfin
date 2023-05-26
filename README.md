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
# https://curl.se/windows/dl-8.1.1_1/curl-8.1.1_1-win64-mingw.zip

cmake -B build_pc -G 'MinGW Makefiles' -DPLATFORM_DESKTOP=ON
mingw32-make -C build_pc -j$(nproc)

# build deps
cmake -B build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/c/MinGW64
mingw32-make -C build -j$(nproc) install
```



## Lanuch Jellyfin Server

```bash
docker run --restart always --name jf1 --tmpfs /cache -v jf1:/config \
  -v /media:/media --add-host api.themoviedb.org:18.65.168.121 \
  --device /dev/dri/renderD128:/dev/dri/renderD128 --device /dev/dri/card0:/dev/dri/card0 \
  -l 'traefik.http.routers.jf1.rule=Host(`jf1.domain.com`)' \
  -l 'traefik.http.services.jf1.loadbalancer.server.port=8096' \
  -l 'traefik.enable=true' --network kind -m 1GB -d jellyfin/jellyfin
```