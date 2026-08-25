#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 COMPACT_PROBE_BUNDLE" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
bundle=$(realpath -- "$1")
arm_build=${KRKRVITA_ARM_BUILD_DIR:-$source_root/build-arm-host}
board_wrapper=${KRKRVITA_A9_WRAPPER:?Set KRKRVITA_A9_WRAPPER to the board runner}
runner=$arm_build/krkrvita-retail-armv7-runtime
build_jobs=${KRKRVITA_BUILD_JOBS:-12}

[[ -d $bundle && -f $bundle/manifest.txt ]] || {
    echo "compact probe bundle has no manifest.txt" >&2
    exit 2
}
[[ -x $board_wrapper ]] || {
    echo "Cortex-A9 board wrapper is unavailable: $board_wrapper" >&2
    exit 2
}

file_count=$(find "$bundle" -maxdepth 1 -type f | wc -l)
dir_count=$(find "$bundle" -mindepth 1 -maxdepth 1 -type d | wc -l)
total_bytes=$(du -sb -- "$bundle" | awk '{print $1}')
if ((file_count < 2 || file_count > 64 || dir_count != 0 || total_bytes > 8388608)); then
    echo "probe bundle exceeds flat 64-file/8-MiB policy" >&2
    exit 2
fi
while IFS= read -r file_path; do
    case $file_path in
        */manifest.txt|*.tjs) ;;
        *) echo "forbidden probe payload: $file_path" >&2; exit 2 ;;
    esac
done < <(find "$bundle" -maxdepth 1 -type f -print)
if find "$bundle" -maxdepth 1 -type f \
    \( -iname '*.xp3' -o -iname '*.exe' -o -iname '*.dll' -o \
       -iname '*.png' -o -iname '*.ogg' -o -iname '*.wav' -o \
       -iname '*.ksd' \) -print -quit | grep -q .; then
    echo "game/archive/media/save payloads are forbidden on the board lane" >&2
    exit 2
fi

cmake --build "$arm_build" -j"$build_jobs" --target krkrvita-retail-armv7-runtime
DATA_ROOT=$bundle "$board_wrapper" "$runner"
