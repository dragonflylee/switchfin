# Switchfin

Switch/PC平台 Jellyfin 客户端

## Building for Switch

To build for Switch, a standard development environment must first be set up. In order to do so, [refer to the Getting Started guide](https://devkitpro.org/wiki/Getting_Started).

```bash
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch Switchfin.nro -j$(nproc)
```

## Building for MinGW64

```bash
cmake -B build_pc -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release -DWIN32_TERMINAL=ON -G "MinGW Makefiles"
mingw32-make -C build_pc -j$(nproc)
```