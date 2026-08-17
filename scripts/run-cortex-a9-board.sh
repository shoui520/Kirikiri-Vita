#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 STATIC_ARMV7_BINARY" >&2
    exit 2
fi

binary=$1
board=${KRKRVITA_CORTEX_A9_HOST:-a9linux}
max_bytes=${KRKRVITA_CORTEX_A9_MAX_BYTES:-33554432}
remote_base=${KRKRVITA_CORTEX_A9_TMPDIR:-/dev/shm}

if [[ ! -f $binary ]]; then
    echo "Cortex-A9 binary does not exist: $binary" >&2
    exit 2
fi

binary_size=$(stat -c '%s' -- "$binary")
if ((binary_size <= 0 || binary_size > max_bytes)); then
    echo "Cortex-A9 binary size $binary_size is outside the permitted 1..$max_bytes byte staging range" >&2
    exit 2
fi

case $remote_base in
    /dev/shm|/tmp|/run) ;;
    *)
        echo "Cortex-A9 temporary base is not an approved volatile filesystem: $remote_base" >&2
        exit 2
        ;;
esac

remote_dir=$(ssh -- "$board" "mktemp -d '$remote_base/krkrvita-a9.XXXXXX'")
case $remote_dir in
    "$remote_base"/krkrvita-a9.*) ;;
    *)
        echo "Cortex-A9 board returned an unsafe temporary path: $remote_dir" >&2
        exit 2
        ;;
esac

cleanup() {
    ssh -- "$board" "rm -rf -- '$remote_dir'" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

available_kib=$(ssh -- "$board" "df -Pk '$remote_dir' | awk 'END { print \$4 }'")
required_kib=$(((binary_size + 1023) / 1024 + 64))
if [[ ! $available_kib =~ ^[0-9]+$ ]] || ((available_kib < required_kib)); then
    echo "Cortex-A9 volatile staging has ${available_kib:-unknown} KiB free; $required_kib KiB is required" >&2
    exit 2
fi

scp -q -- "$binary" "$board:$remote_dir/probe"
echo "Cortex-A9 board: host=$board binary=$(basename -- "$binary") bytes=$binary_size"
ssh -- "$board" "chmod 700 '$remote_dir/probe' && '$remote_dir/probe'"
