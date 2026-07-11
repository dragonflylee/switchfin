#!/usr/bin/env bash
# Standalone logic tests for pleNx (no CMake wiring; BUILD_TESTING is OFF).
# Compiles and runs every tests/test_*.cpp. Header-only pieces (plex types,
# offline catalog logic) that don't pull in Borealis/mpv can be exercised here.
#
#   ./tests/run.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INC_APP="$ROOT/app/include"
INC_JSON="$ROOT/library/borealis/library/include/borealis/extern"
CXX="${CXX:-c++}"
ARCH="${ARCH:-x86_64}"
OUT="$(mktemp -d)"
rc=0

for src in "$ROOT"/tests/test_*.cpp; do
    name="$(basename "$src" .cpp)"
    bin="$OUT/$name"
    if ! "$CXX" -std=gnu++17 -arch "$ARCH" -Wall -I"$INC_APP" -I"$INC_JSON" "$src" -o "$bin"; then
        echo "COMPILE FAIL: $name"
        rc=1
        continue
    fi
    if ! "$bin"; then
        rc=1
    fi
done

exit "$rc"
