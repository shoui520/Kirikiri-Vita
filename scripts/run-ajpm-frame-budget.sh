#!/usr/bin/env bash
# Measure what an AJPM frame costs on a Cortex-A9.
#
# AlphaMovie playback has a 33.3 ms budget per frame and each 1024x768 frame
# carries a 768 KB zlib-compressed alpha plane. Whether that fits is a property
# of the CPU rather than of the port, so it is worth measuring before building
# a playback path rather than after.
#
# The probe synthesizes its own frame data and never reads game content, so
# nothing from the retail corpus is staged on the board.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
cxx=${KRKRVITA_ARM_CXX:-arm-linux-gnueabihf-g++}
out=${KRKRVITA_AJPM_BUDGET_OUT:-$(mktemp -d)/krkrvita-ajpm-budget}

if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "ARM cross compiler not found: $cxx" >&2
    exit 2
fi

# Link the VitaSDK's own zlib rather than an armhf distribution build, so the
# inflate being measured is the one the Vita backend actually calls. The probe
# declares zlib's entry points itself, so no VitaSDK headers are involved.
if [[ -n ${KRKRVITA_ARM_ZLIB:-} ]]; then
    zlib=$KRKRVITA_ARM_ZLIB
elif [[ -n ${VITASDK:-} ]]; then
    zlib=$VITASDK/arm-vita-eabi/lib/libz.a
else
    echo "set KRKRVITA_ARM_ZLIB or VITASDK to locate the ARM zlib archive" >&2
    exit 2
fi
if [[ ! -f $zlib ]]; then
    echo "zlib archive to measure not found: $zlib" >&2
    exit 2
fi

"$cxx" -O2 -std=c++20 -static -marm -march=armv7-a -mfpu=neon -mfloat-abi=hard \
    -o "$out" \
    "$source_root/tests/test_ajpm_frame_budget.cpp" \
    "$zlib" 2>&1 | grep -v 'variable-size enums\|GNU-stack\|behaviour is deprecated' || true

if [[ ! -x $out ]]; then
    echo "the ARMv7 frame-budget probe did not link" >&2
    exit 1
fi

echo "built ARMv7 AJPM frame-budget probe: $out"
exec "$script_dir/run-cortex-a9-board.sh" "$out"
