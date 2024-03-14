#!/bin/bash

set -e

: "${VERSION_BUILD:="0"}"
: "${CMAKE_PREFIX_PATH:="/opt/switchfin"}"
export PKG_CONFIG_PATH=$CMAKE_PREFIX_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$CMAKE_PREFIX_PATH/lib

mkdir -p /tmp/deb/DEBIAN /tmp/deb/usr /tmp/deb/opt/switchfin/lib

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH  \
  -DPLATFORM_DESKTOP=ON -DUSE_SYSTEM_GLFW=ON -DCMAKE_INSTALL=ON -DVERSION_BUILD=$VERSION_BUILD \
  -DCUSTOM_RESOURCES_DIR=$CMAKE_PREFIX_PATH -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib
cmake --build build -j$(nproc)
DESTDIR="/tmp/deb" cmake --install build --strip

cp -d /usr/lib/aarch64-linux-gnu/libstdc++.so.* /opt/switchfin/lib/*.so.* /tmp/deb/opt/switchfin/lib
mv /tmp/deb/opt/switchfin/share /tmp/deb/usr
sed -i 's|Exec=Switchfin|Exec=/opt/switchfin/bin/Switchfin|' /tmp/deb/usr/share/applications/org.player.switchfin.desktop
cp scripts/deb/kylin-v10/control /tmp/deb/DEBIAN
dpkg --build /tmp/deb Switchfin-kylin-v10-aarch64.deb
