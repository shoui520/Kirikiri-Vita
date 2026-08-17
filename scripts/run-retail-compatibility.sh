#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
manifest=${KRKRVITA_RETAIL_MANIFEST:-$source_root/tests/retail_compatibility_manifest.txt}
runner=${KRKRVITA_RETAIL_RUNNER:-$source_root/build-host/krkrvita-retail-compatibility}
require_all=0
if [[ ${1:-} == --require-all ]]; then
    require_all=1
    shift
fi
if (($#)); then
    echo "usage: $0 [--require-all]" >&2
    exit 2
fi
if [[ ! -x $runner ]]; then
    echo "compatibility runner not built: $runner" >&2
    exit 2
fi

passed=0
phase2_blocked=0
runtime_blocked=0
failed=0
missing=0
manifest_errors=0
rows=0
declare -A seen_ids=()
declare -A seen_paths=()
while IFS= read -r row || [[ -n $row ]]; do
    [[ -n $row && ${row:0:1} != '#' ]] || continue
    IFS='|' read -r -a fields <<< "$row"
    if ((${#fields[@]} != 8)); then
        echo "MALFORMED manifest row: $row" >&2
        ((manifest_errors += 1))
        continue
    fi
    game_id=${fields[0]}
    game_path=${fields[1]}
    fingerprint=${fields[2]}
    expectation=${fields[3]}
    rule=${fields[4]}
    recognized=${fields[5]}
    samples=${fields[6]}
    expected_diagnostic=${fields[7]}
    if [[ ! $game_id =~ ^[a-z0-9_]+$ ||
          ! $fingerprint =~ ^[0-9a-f]{64}$ ||
          ! $recognized =~ ^[0-9]+$ || ! $samples =~ ^[0-9]+$ ]]; then
        echo "INVALID manifest fields: $row" >&2
        ((manifest_errors += 1))
        continue
    fi
    if [[ -n ${seen_ids[$game_id]:-} || -n ${seen_paths[$game_path]:-} ]]; then
        echo "DUPLICATE manifest identity: $row" >&2
        ((manifest_errors += 1))
        continue
    fi
    seen_ids[$game_id]=1
    seen_paths[$game_path]=1
    ((rows += 1))
    case $expectation in
        phase1|phase2)
            if [[ $expected_diagnostic != - ]]; then
                echo "UNEXPECTED diagnostic on $expectation row: $game_id" >&2
                ((manifest_errors += 1))
                continue
            fi
            ;;
        runtime_blocked)
            if [[ -z $expected_diagnostic || $expected_diagnostic == - ]]; then
                echo "MISSING runtime blocker diagnostic: $game_id" >&2
                ((manifest_errors += 1))
                continue
            fi
            ;;
        *)
            echo "INVALID manifest expectation: $game_id $expectation" >&2
            ((manifest_errors += 1))
            continue
            ;;
    esac
    if [[ ! -d $game_path ]]; then
        echo "MISSING $game_id $game_path" >&2
        ((missing += 1))
        continue
    fi
    echo "=== $game_id ==="
    if [[ $expectation == phase1 ]]; then
        if "$runner" --game "$game_path" "$fingerprint" "$rule" \
                "$recognized" "$samples"; then
            ((passed += 1))
        else
            echo "FAILED $game_id" >&2
            ((failed += 1))
        fi
    elif [[ $expectation == runtime_blocked ]]; then
        if "$runner" --expect-runtime-blocker "$game_path" "$fingerprint" \
                "$rule" "$recognized" "$samples" "$expected_diagnostic"; then
            ((runtime_blocked += 1))
        else
            echo "FAILED $game_id (runtime blocker expectation changed)" >&2
            ((failed += 1))
        fi
    elif [[ $expectation == phase2 ]]; then
        if "$runner" --expect-phase2 "$game_path" "$fingerprint"; then
            ((phase2_blocked += 1))
        else
            echo "FAILED $game_id (Phase 2 expectation changed)" >&2
            ((failed += 1))
        fi
    else
        echo "invalid manifest expectation for $game_id: $expectation" >&2
        exit 2
    fi
done < "$manifest"

echo "retail matrix: $rows titles, $passed compatible, $runtime_blocked runtime-blocked, $phase2_blocked phase-2 blocked, $failed failed, $missing missing"
if ((manifest_errors || failed || missing ||
      (require_all && (runtime_blocked || phase2_blocked)))); then
    echo "compatibility gate failed: $manifest_errors manifest errors, $failed unexpected failures, $runtime_blocked runtime blockers, $phase2_blocked phase-2 blockers, $missing missing" >&2
    exit 1
fi
