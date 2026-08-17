#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 PATH_TO_BOOT_STATUS_TXT" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cmake -DTRACE="$1" -P "$repo_root/cmake/VerifyHardwareBootTrace.cmake"
