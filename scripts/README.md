# Build Depency

### build for macOS

```shell
# https://pkg-config.freedesktop.org/releases/pkg-config-0.29.2.tar.gz
LDFLAGS="-framework CoreFoundation -framework Carbon" ./configure --with-internal-glib

# https://github.com/ninja-build/ninja/releases/download/v1.12.0/ninja-mac.zip
# https://www.nasm.us/pub/nasm/releasebuilds/2.16.01/macosx/nasm-2.16.01-macosx.zip
```

### generate icons

```shell
sudo apt-get install -y librsvg2-bin
for size in 32 48 64 128 256; do
    icon_path="icons/${size}x${size}"
    mkdir -p ${icon_path}
    rsvg-convert -w ${size} -h ${size} -o ${icon_path}/org.player.switchfin.png org.player.switchfin.svg
done
```