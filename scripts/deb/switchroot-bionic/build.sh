#!/bin/bash

set -e

: "${VERSION_BUILD:="0"}"
: "${CMAKE_PREFIX_PATH:="/opt/switchfin"}"
export PKG_CONFIG_PATH=$CMAKE_PREFIX_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$CMAKE_PREFIX_PATH/lib:/usr/lib/aarch64-linux-gnu/tegra

wget -qO- https://curl.se/download/curl-8.7.1.tar.xz | tar Jxf - -C /tmp
git clone https://gitlab.com/switchroot/switch-l4t-multimedia/FFmpeg.git --depth=1 /tmp/ffmpeg
git clone https://gitlab.com/switchroot/switch-l4t-multimedia/mpv.git --depth=1 /tmp/mpv
git clone https://github.com/dragonflylee/glfw.git -b switchfin --depth=1 /tmp/glfw

cd /tmp/curl-8.7.1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DCURL_USE_OPENSSL=ON \
  -DHTTP_ONLY=ON -DCURL_DISABLE_PROGRESS_METER=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
  -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF -DBUILD_LIBCURL_DOCS=OFF
cmake --build build
cmake --install build --strip

cd /tmp/ffmpeg
./configure --prefix=$CMAKE_PREFIX_PATH --enable-shared --disable-static \
  --extra-cflags='-march=armv8-a+simd+crypto+crc -mtune=cortex-a57 -I/usr/src/jetson_multimedia_api/include' \
  --extra-ldflags='-L/usr/lib/aarch64-linux-gnu/tegra' \
  --extra-libs='-lpthread -lm -lnvbuf_utils -lv4l2' \
  --ld=g++ --enable-nonfree --enable-openssl --enable-libv4l2 --enable-nvv4l2 \
  --enable-opengl --disable-doc --enable-asm --enable-neon --disable-debug \
  --enable-libass --enable-demuxer=hls --disable-muxers --disable-avdevice \
  --disable-protocols --enable-protocol=file,http,tcp,udp,hls,https,tls,httpproxy \
  --disable-filters --enable-filter=hflip,vflip,transpose \
  --disable-encoders --disable-programs --enable-rpath
make -j$(nproc)
make install

cd /tmp/mpv
./bootstrap.py
LIBDIR=$CMAKE_PREFIX_PATH/lib RPATH=$CMAKE_PREFIX_PATH/lib ./waf configure --prefix=$CMAKE_PREFIX_PATH \
  --disable-libmpv-static --enable-libmpv-shared --disable-debug-build --disable-libavdevice \
  --disable-cplayer --disable-libarchive --disable-lua
./waf install

cd /tmp/glfw
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DGLFW_BUILD_WAYLAND=OFF \
  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
cmake --build build
cmake --install build --strip

cd /opt
mkdir -p /tmp/deb/DEBIAN /tmp/deb/usr /tmp/deb/opt/switchfin/lib
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH  \
  -DPLATFORM_DESKTOP=ON -DUSE_SYSTEM_GLFW=ON -DCMAKE_INSTALL=ON -DVERSION_BUILD=$VERSION_BUILD \
  -DCUSTOM_RESOURCES_DIR=$CMAKE_PREFIX_PATH -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib
cmake --build build -j$(nproc)
DESTDIR="/tmp/deb" cmake --install build --strip

cp -d /opt/switchfin/lib/*.so.* /tmp/deb/opt/switchfin/lib
mv /tmp/deb/opt/switchfin/share /tmp/deb/usr
sed -i 's|Exec=Switchfin|Exec=/opt/switchfin/bin/Switchfin|' /tmp/deb/usr/share/applications/org.player.switchfin.desktop
cp scripts/deb/switchroot-bionic/control /tmp/deb/DEBIAN
dpkg --build /tmp/deb Switchfin-switchroot-ubuntu-$(uname -m).deb
