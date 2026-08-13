#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:=/home/shoui/vitasdk}"
export VITASDK

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$repo_root/build-vita-engine"
booter_build_dir="$repo_root/build-vita-booter"

"$repo_root/scripts/apply-upstream-patches.sh"

cmake -S "$repo_root/vendor/krkrsdl2" -B "$build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPTION_ENABLE_EXTERNAL_PLUGINS=OFF \
  -DOPTION_ENABLE_ASYNC_IMAGE_LOAD=OFF \
  -DVIDEO_VITA_GXM=OFF \
  -DVIDEO_VITA_PIB=OFF \
  -DVIDEO_VITA_PVR=OFF \
  -DKRKRVITA_OVERLAY_DIR="$repo_root" \
  -DVITA_APP_NAME="Kirikiri Vita" \
  -DVITA_TITLEID=KRVITA001 \
  -DVITA_VERSION=01.01
cmake --build "$build_dir" --parallel "${JOBS:-4}"

cmake -S "$repo_root/vita/booter" -B "$booter_build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DKRKRVITA_ROOT="$repo_root"
cmake --build "$booter_build_dir" --parallel "${JOBS:-4}"

echo "VPK: $build_dir/krkrsdl2.vpk"
echo "Bubble template: $booter_build_dir/template"
