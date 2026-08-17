#!/usr/bin/env bash
set -euo pipefail

if (($#)); then
    echo "usage: $0" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
temporary_root=$(mktemp -d /tmp/krkrvita-retail-a9.XXXXXX)
cleanup() {
    case $temporary_root in
        /tmp/krkrvita-retail-a9.*) rm -rf -- "$temporary_root" ;;
        *) echo "refusing to clean unexpected temporary path" >&2 ;;
    esac
}
trap cleanup EXIT

"$script_dir/prepare-retail-armv7-bundle.sh" "$temporary_root/bundle"
"$script_dir/run-cortex-a9-compatibility.sh" "$temporary_root/bundle"
