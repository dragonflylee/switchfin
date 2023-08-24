# Build Depency

### build webp for mingw64

```bash
# https://github.com/glennrp/libpng/archive/v1.6.40.zip

cmake -B build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/MinGW64 -DPNG_SHARED=OFF -DPNG_EXECUTABLES=OFF -DPNG_TESTS=OFF
mingw32-make -C build install

# https://github.com/webmproject/libwebp/archive/v1.3.1.zip
cmake -B build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/MinGW64 -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
  -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
  -DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
mingw32-make -C build install
```