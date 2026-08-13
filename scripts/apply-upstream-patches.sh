#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

apply_patch_once() {
  local target="$1"
  local patch_file="$2"
  if git -C "$target" apply --reverse --check --ignore-space-change \
      "$patch_file" >/dev/null 2>&1; then
    echo "already applied: ${patch_file#"$repo_root"/}"
    return
  fi
  if ! git -C "$target" apply --check --ignore-space-change "$patch_file"; then
    echo "patch does not apply cleanly: $patch_file" >&2
    echo "restore or update the pinned submodule before building" >&2
    exit 1
  fi
  git -C "$target" apply --ignore-space-change "$patch_file"
  echo "applied: ${patch_file#"$repo_root"/}"
}

apply_patch_once "$repo_root/vendor/krkrsdl2" \
  "$repo_root/patches/krkrsdl2-vita.patch"
apply_patch_once "$repo_root/vendor/krkrsdl2/external/SDL" \
  "$repo_root/patches/sdl-vita.patch"
apply_patch_once "$repo_root/vendor/krkrsdl2/external/krkrz" \
  "$repo_root/patches/krkrz-retail.patch"
