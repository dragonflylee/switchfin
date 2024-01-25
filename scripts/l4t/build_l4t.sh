#!/bin/bash

set -e

export CMAKE_PREFIX_PATH=/opt/switchfin
export PKG_CONFIG_PATH=$CMAKE_PREFIX_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$CMAKE_PREFIX_PATH/lib:/usr/lib/aarch64-linux-gnu/tegra

mkdir -p /tmp/deb/usr /tmp/deb/opt/switchfin/lib
sed -i 's|Exec=Switchfin|Exec=/opt/switchfin/bin/Switchfin|' scripts/switchfin.desktop

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CMAKE_PREFIX_PATH  \
  -DPLATFORM_DESKTOP=ON -DUSE_SYSTEM_CURL=ON -DUSE_SYSTEM_GLFW=ON -DCMAKE_INSTALL=ON \
  -DCUSTOM_RESOURCES_DIR=$CMAKE_PREFIX_PATH -DCMAKE_INSTALL_RPATH=$CMAKE_PREFIX_PATH/lib
cmake --build build
DESTDIR="/tmp/deb" cmake --install build --strip

cp -r /opt/switchfin/lib/*.so.* /tmp/deb/opt/switchfin/lib
cp -r scripts/l4t/DEBIAN /tmp/deb
mv /tmp/deb/opt/switchfin/share /tmp/deb/usr
dpkg --build /tmp/deb Switchfin-aarch64-switchroot-ubuntu.deb
