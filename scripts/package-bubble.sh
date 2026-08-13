#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: $0 GAME_DIR OUTPUT_VPK [TITLE_ID]" >&2
  exit 2
fi

: "${VITASDK:=/home/shoui/vitasdk}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
game_dir="$1"
output_vpk="$2"
title_id="${3:-}"
stage_dir="$repo_root/build-bubble-stage"
tool="$repo_root/build-host/krkrvita-tool"
template="$repo_root/build-vita-booter/template"

if [[ ! -x "$tool" ]]; then
  echo "Missing host tool; build it with cmake --build build-host -j4" >&2
  exit 1
fi
if [[ ! -f "$template/eboot.bin" ]]; then
  echo "Missing bubble template; run scripts/build-vita.sh first" >&2
  exit 1
fi

if [[ -n "$title_id" ]]; then
  "$tool" bubble-stage "$game_dir" "$template" "$stage_dir" "$title_id"
else
  "$tool" bubble-stage "$game_dir" "$template" "$stage_dir"
fi

"$VITASDK/bin/vita-pack-vpk" \
  -s "$stage_dir/sce_sys/param.sfo" \
  -b "$stage_dir/eboot.bin" \
  -a "$stage_dir/game.id=game.id" \
  -a "$stage_dir/sce_sys/icon0.png=sce_sys/icon0.png" \
  -a "$stage_dir/sce_sys/livearea/contents=sce_sys/livearea/contents" \
  "$output_vpk"

echo "Bubble VPK: $output_vpk"
