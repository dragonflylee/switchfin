#!/usr/bin/env bash
# Construit pleNx.nro pour Nintendo Switch via Docker (image devkitPro),
# en répliquant le job `build-nx` de .github/workflows/build.yaml.
#
# Usage :
#   ./scripts/build-switch.sh             # pilote deko3d (recommandé)
#   DRIVER=opengl ./scripts/build-switch.sh
#
# prod.keys : si ~/.switch/prod.keys existe, il est utilisé pour le forwarder
# NSP ; sinon le CMake du forwarder en télécharge un public (comme la CI).

set -euo pipefail

cd "$(dirname "$0")/.."

DRIVER="${DRIVER:-deko3d}"
IMAGE="devkitpro/devkita64:20260219"
BASE_URL="https://github.com/dragonflylee/switchfin/releases/download/switch-portlibs"
BUILD_DIR="build_switch_${DRIVER}"

CMAKE_EXTRA=""
if [ "$DRIVER" = "deko3d" ]; then
    CMAKE_EXTRA="-DUSE_DEKO3D=ON"
    LIBMPV_PKG="switch-libmpv-deko3d-0.36.0-5-any.pkg.tar.zst"
else
    LIBMPV_PKG="switch-libmpv-0.36.0-5-any.pkg.tar.zst"
fi

# Docker Desktop ne partage pas ~/.switch : copie temporaire des clés dans
# l'arbre du projet (gitignorée, supprimée en sortie de script)
KEYSET_ARG=""
if [ -f "$HOME/.switch/prod.keys" ]; then
    echo ">> prod.keys local détecté (~/.switch/prod.keys)"
    cp "$HOME/.switch/prod.keys" .prod.keys.tmp
    trap 'rm -f .prod.keys.tmp' EXIT
    KEYSET_ARG="-DPROJECT_KEYSET=/src/.prod.keys.tmp"
fi

docker run --rm --platform linux/amd64 \
    -v "$PWD:/src" -w /src "$IMAGE" bash -ec "
    git config --system --add safe.directory /src || true

    if [ '$DRIVER' = 'deko3d' ]; then
        dkp-pacman --noconfirm -R switch-libmpv 2>/dev/null || true
        dkp-pacman --noconfirm -U $BASE_URL/libuam-master-1-any.pkg.tar.zst
    fi
    dkp-pacman --noconfirm -U $BASE_URL/hacBrewPack-3.05-1-x86_64.pkg.tar.zst
    for pkg in switch-mbedtls-3.6.5-1-any switch-libssh2-1.11.1-1-any switch-dav1d-1.5.3-1-any \
               switch-curl-8.16.0-2-any switch-ffmpeg-7.1.4-5-any switch-nspmini-main-1-any; do
        dkp-pacman --noconfirm -U $BASE_URL/\${pkg}.pkg.tar.zst
    done
    dkp-pacman --noconfirm -U $BASE_URL/$LIBMPV_PKG

    git clone https://github.com/DarkMatterCore/libusbhsfs.git --depth=1 /tmp/libusbhsfs
    make -C /tmp/libusbhsfs BUILD_TYPE=GPL install -j\$(nproc)

    cmake -B $BUILD_DIR $CMAKE_EXTRA $KEYSET_ARG \
        -DCMAKE_BUILD_TYPE=Release \
        -DPLATFORM_SWITCH=ON \
        -DUSE_LIBUSBHSFS=ON \
        -DBUILTIN_NSP=ON
    make -C $BUILD_DIR pleNx.nro -j\$(nproc)
"

echo ''
echo ">> OK : $BUILD_DIR/pleNx.nro"
