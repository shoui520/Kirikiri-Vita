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
  -DVITA_VERSION=01.07
cmake --build "$build_dir" --parallel "${JOBS:-4}"

# Internal Yuri modules are activated at runtime through Plugins.link().  Make
# the build fail if registration data is accidentally removed or dead-stripped.
required_internal_modules=(
  addFont.dll csvParser.dll dirlist.dll fftgraph.dll getSample.dll
  getabout.dll perspective.dll saveStruct.dll varfile.dll win32dialog.dll
  wutcwf.dll xp3filter.dll
)
linked_wide_strings="$(strings -el "$build_dir/krkrsdl2")"
for module in "${required_internal_modules[@]}"; do
  if ! rg -j1 -F -x -q -- "$module" <<<"$linked_wide_strings"; then
    echo "missing Yuri internal module in Vita engine: $module" >&2
    exit 1
  fi
done

# Keep the non-plugin half of the Yuri compatibility contract honest too.
# These factories are registered as global TJS classes during engine startup.
required_yuri_native_factories=(
  TVPCreateNativeClass_CDDASoundBuffer
  TVPCreateNativeClass_MIDISoundBuffer
  TVPCreateNativeClass_Pad
  TVPCreateNativeClass_KAGParser
  TVPCreateNativeClass_MenuItem
  TVPCreateNativeClass_SoundBuffer
)
linked_symbols="$($VITASDK/bin/arm-vita-eabi-nm -C --defined-only "$build_dir/krkrsdl2")"
for factory in "${required_yuri_native_factories[@]}"; do
  if ! rg -j1 -F -q -- "$factory" <<<"$linked_symbols"; then
    echo "missing Yuri native-class factory in Vita engine: $factory" >&2
    exit 1
  fi
done

cmake -S "$repo_root/vita/booter" -B "$booter_build_dir" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DKRKRVITA_ROOT="$repo_root"
cmake --build "$booter_build_dir" --parallel "${JOBS:-4}"

echo "VPK: $build_dir/krkrsdl2.vpk"
echo "Bubble template: $booter_build_dir/template"
