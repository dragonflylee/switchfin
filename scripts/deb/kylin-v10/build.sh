#!/bin/bash

set -e

: "${VERSION_BUILD:="0"}"
: "${CMAKE_PREFIX_PATH:="/opt/switchfin"}"
export PKG_CONFIG_PATH=$CMAKE_PREFIX_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$CMAKE_PREFIX_PATH/lib

wget -qO- https://curl.se/download/curl-8.7.1.tar.xz | tar Jxf - -C /tmp
wget -qO- https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz | tar Jxf - -C /tmp
wget -qO- https://github.com/mpv-player/mpv/archive/v0.36.0.tar.gz | tar zxf - -C /tmp
wget -qO- https://github.com/webmproject/libwebp/archive/v1.4.0.tar.gz | tar zxf - -C /tmp
git clone https://github.com/dragonflylee/glfw.git -b switchfin --depth=1 /tmp/glfw

cd /tmp/curl-8.7.1
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DCURL_USE_OPENSSL=ON \
  -DHTTP_ONLY=ON -DCURL_DISABLE_PROGRESS_METER=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
  -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF -DBUILD_LIBCURL_DOCS=OFF
cmake --build build -j$(nproc)
cmake --install build --strip

cd /tmp/ffmpeg-7.1
./configure --prefix=$CMAKE_PREFIX_PATH --enable-shared --disable-static \
  --ld=g++ --enable-nonfree --enable-openssl --enable-libv4l2 \
  --enable-opengl --enable-libass --disable-doc --enable-asm --enable-rpath \
  --disable-protocols --enable-protocol=file,http,tcp,udp,hls,https,tls,httpproxy \
  --disable-filters --enable-filter=hflip,vflip,transpose --disable-avdevice \
  --disable-muxers --disable-encoders --disable-programs --disable-debug
make -j$(nproc)
make install

cd /tmp/mpv-0.36.0
patch -Nbp1 -i /opt/scripts/mingw64/mpv/0002-lavfi-channel-layout.patch
./bootstrap.py
LIBDIR=$CMAKE_PREFIX_PATH/lib RPATH=$CMAKE_PREFIX_PATH/lib ./waf configure --prefix=$CMAKE_PREFIX_PATH \
  --disable-libmpv-static --enable-libmpv-shared --disable-debug-build --disable-libavdevice \
  --disable-cplayer --disable-libarchive --disable-lua
./waf install

cd /tmp/libwebp-1.4.0
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DWEBP_BUILD_EXTRAS=OFF \
  -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
  -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_LIBWEBPMUX=OFF
cmake --build build -j$(nproc)
cmake --install build --strip

cd /tmp/glfw
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH \
  -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib -DBUILD_SHARED_LIBS=ON -DGLFW_BUILD_WAYLAND=OFF \
  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
cmake --build build -j$(nproc)
cmake --install build --strip

cd /opt
mkdir -p /tmp/deb/DEBIAN /tmp/deb/usr /tmp/deb/opt/switchfin/lib
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH  \
  -DPLATFORM_DESKTOP=ON -DUSE_SYSTEM_GLFW=ON -DCMAKE_INSTALL=ON -DVERSION_BUILD=$VERSION_BUILD \
  -DCUSTOM_RESOURCES_DIR=$CMAKE_PREFIX_PATH -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib
cmake --build build -j$(nproc)
DESTDIR="/tmp/deb" cmake --install build --strip

cp -d /usr/lib/$(uname -m)-linux-gnu/libstdc++.so.* /opt/switchfin/lib/*.so.* /tmp/deb/opt/switchfin/lib
mv /tmp/deb/opt/switchfin/share /tmp/deb/usr
sed -i 's|Exec=Switchfin|Exec=/opt/switchfin/bin/Switchfin|' /tmp/deb/usr/share/applications/org.player.switchfin.desktop
cp scripts/deb/kylin-v10/control /tmp/deb/DEBIAN
dpkg --build /tmp/deb Switchfin-kylin-v10-$(uname -m).deb
