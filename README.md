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
cmake -B build_pc -G 'MinGW Makefiles' -DPLATFORM_DESKTOP=ON
mingw32-make -C build_pc -j$(nproc)
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