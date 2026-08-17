#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 NEW_OUTPUT_DIRECTORY" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
manifest=${KRKRVITA_RETAIL_MANIFEST:-$source_root/tests/retail_compatibility_manifest.txt}
runner=${KRKRVITA_RETAIL_RUNNER:-$source_root/build-host/krkrvita-retail-compatibility}
output=$1
if [[ -e $output ]]; then
    echo "output already exists; refusing to merge or overwrite: $output" >&2
    exit 2
fi
mkdir -p -- "$output"

emitted=0
while IFS='|' read -r game_id game_path fingerprint expectation rule recognized samples expected_diagnostic; do
    [[ -n $game_id && ${game_id:0:1} != '#' ]] || continue
    if [[ $expectation != phase1 ]]; then
        echo "$expectation: $game_id (no retail payload exported)"
        continue
    fi
    "$runner" --emit-arm-probe "$game_path" "$fingerprint" "$rule" \
        "$recognized" "$samples" "$output" "$game_id"
    ((emitted += 1))
done < "$manifest"

file_count=$(find "$output" -maxdepth 1 -type f | wc -l)
total_bytes=$(du -sb -- "$output" | awk '{print $1}')
echo "ARMv7 bundle: $emitted startup probes, $file_count files, $total_bytes bytes"
if ((emitted == 0 || file_count > 64 || total_bytes > 8388608)); then
    echo "generated ARMv7 bundle violates compact-board policy" >&2
    exit 1
fi
