# Build Depency

### build webp for mingw64

```bash
# https://github.com/webmproject/libwebp/archive/v1.3.1.zip
cmake -B build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/MinGW64 -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
  -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
  -DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
mingw32-make -C build install

# https://github.com/curl/curl/releases/download/curl-7_88_1/curl-7.88.1.tar.xz
cmake -B build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/MinGW64 -DBUILD_SHARED_LIBS=OFF \
  -DHTTP_ONLY=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DCURL_DISABLE_PROGRESS_METER=ON -DCURL_USE_SCHANNEL=ON \
  -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF
mingw32-make -C build install
```

### build for macos

```bash
# https://pkg-config.freedesktop.org/releases/pkg-config-0.29.2.tar.gz
LDFLAGS="-framework CoreFoundation -framework Carbon" ./configure --with-internal-glib

# https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-mac.zip
# https://www.nasm.us/pub/nasm/releasebuilds/2.16.01/macosx/nasm-2.16.01-macosx.zip
```
