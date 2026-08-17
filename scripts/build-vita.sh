#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:=/home/shoui/vitasdk}"
: "${JOBS:=12}"
export VITASDK

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
host_build="$repo_root/build-host"
sanitizer_build="$repo_root/build-host-asan"
vita_build="$repo_root/build-vita-yuri"
sample_game="${KRKRVITA_RETAIL_TEST_GAME:-/home/shoui/Agents/CodexMax/色情教団}"
patch_snapshot="${KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT:-$repo_root/.cache/patches/0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300}"

cmake -S "$repo_root" -B "$host_build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DKRKRVITA_BUILD_TESTS=ON \
  -DKRKRVITA_RETAIL_TEST_GAME="$sample_game" \
  -DKRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT="$patch_snapshot"
cmake --build "$host_build" --parallel "$JOBS"
ctest --test-dir "$host_build" --output-on-failure --parallel 1

cmake -S "$repo_root" -B "$sanitizer_build" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKRKRVITA_BUILD_TESTS=ON \
  -DKRKRVITA_RETAIL_TEST_GAME="$sample_game" \
  -DKRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT="$patch_snapshot" \
  -DCMAKE_C_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build "$sanitizer_build" --parallel "$JOBS"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir "$sanitizer_build" --output-on-failure --parallel 1

cmake -S "$repo_root" -B "$vita_build" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$vita_build" \
  --target krkrvita-yuri.vpk-vpk --parallel "$JOBS"

sha256sum "$vita_build/krkrvita-yuri.vpk"
echo "Validated VPK: $vita_build/krkrvita-yuri.vpk"
