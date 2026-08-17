#!/usr/bin/env bash
# Build and run the TVPGL ARM/NEON differential probe on the Cortex-A9 board.
#
# tests/test_yuri_arm_alpha.cpp needs real ARM NEON, so it cannot run under
# ctest on an x86 host. It was previously not referenced by any build file at
# all, which is why the gap it was written to close went unnoticed: the test
# existed but never ran, and it compared the _HDA blend variants rather than
# the plain ones the engine actually calls for an opaque destination.
#
# The probe fails only on a colour-channel difference. An alpha-only difference
# is the documented non-HDA contract (destination alpha is not held) and is not
# a rendering defect.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
cxx=${KRKRVITA_ARM_CXX:-arm-linux-gnueabihf-g++}
out=${KRKRVITA_ARM_PROBE_OUT:-$(mktemp -d)/krkrvita-arm-alpha}

if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "ARM cross compiler not found: $cxx" >&2
    exit 2
fi

visual=$source_root/vendor/yuri/src/core/visual
"$cxx" -O2 -std=c++20 -static -marm -march=armv7-a -mfpu=neon -mfloat-abi=hard \
    -I "$source_root/include" \
    -I "$visual" \
    -I "$source_root/vendor/yuri/src/core/tjs2" \
    -I "$source_root/vendor/yuri/src/core/base" \
    -I "$source_root/vendor/yuri/src/core/utils" \
    -I "$source_root/vendor/yuri/src/core/environ" \
    -o "$out" \
    "$source_root/tests/test_yuri_arm_alpha.cpp" \
    "$visual/tvpgl.cpp" \
    "$visual/ARM/tvpgl_arm.cpp"

echo "built ARMv7 probe: $out"
exec "$script_dir/run-cortex-a9-board.sh" "$out"
