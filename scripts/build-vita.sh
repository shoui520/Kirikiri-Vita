#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:=/home/shoui/vitasdk}"
export VITASDK

cmake -S "$(dirname "$0")/.." -B "$(dirname "$0")/../build-vita" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKRKRVITA_BUILD_TESTS=OFF
cmake --build "$(dirname "$0")/../build-vita" --parallel

