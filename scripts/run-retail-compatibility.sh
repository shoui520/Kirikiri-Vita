#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
manifest=${KRKRVITA_RETAIL_MANIFEST:-$source_root/tests/retail_compatibility_manifest.txt}
evidence=${KRKRVITA_RETAIL_HARDWARE_EVIDENCE:-$source_root/tests/retail_hardware_evidence.txt}
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
if [[ ! -f $evidence ]]; then
    echo "hardware evidence file missing: $evidence" >&2
    exit 2
fi

# Physical-Vita receipts. A hardware_blocked row is only honest if the failure
# it claims is actually recorded, so look the status up rather than trusting
# the manifest alone.
declare -A hw_status=()
declare -A hw_symptom=()
while IFS= read -r receipt || [[ -n $receipt ]]; do
    [[ -n $receipt && ${receipt:0:1} != '#' ]] || continue
    IFS='|' read -r -a hw <<< "$receipt"
    if ((${#hw[@]} != 5)); then
        echo "MALFORMED hardware evidence row: $receipt" >&2
        exit 2
    fi
    case ${hw[3]} in
        passed|blocked) ;;
        *) echo "INVALID hardware evidence status: ${hw[0]} ${hw[3]}" >&2; exit 2 ;;
    esac
    hw_status[${hw[0]}]=${hw[3]}
    hw_symptom[${hw[0]}]=${hw[4]}
done < "$evidence"

passed=0
phase2_blocked=0
runtime_blocked=0
hardware_blocked=0
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
        hardware_blocked)
            if [[ $expected_diagnostic != - ]]; then
                echo "UNEXPECTED host diagnostic on hardware_blocked row: $game_id" >&2
                ((manifest_errors += 1))
                continue
            fi
            if [[ ${hw_status[$game_id]:-} != blocked ]]; then
                echo "MISSING blocked hardware receipt: $game_id" >&2
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
    elif [[ $expectation == hardware_blocked ]]; then
        # The host audit must still pass; the blocker is only visible on the
        # device, so the receipt is what keeps the row honest.
        if "$runner" --game "$game_path" "$fingerprint" "$rule" \
                "$recognized" "$samples"; then
            echo "HARDWARE-BLOCKED $game_id: ${hw_symptom[$game_id]}" >&2
            ((hardware_blocked += 1))
        else
            echo "FAILED $game_id (host audit regressed)" >&2
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

# "host audit passed" is deliberately not "compatible": nothing in this script
# runs on a Vita, so it cannot prove a title works.
echo "retail matrix: $rows titles, $passed host-audit passed, $hardware_blocked hardware-blocked, $runtime_blocked runtime-blocked, $phase2_blocked phase-2 blocked, $failed failed, $missing missing"
if ((manifest_errors || failed || missing ||
      (require_all && (runtime_blocked || phase2_blocked || hardware_blocked)))); then
    echo "compatibility gate failed: $manifest_errors manifest errors, $failed unexpected failures, $hardware_blocked hardware blockers, $runtime_blocked runtime blockers, $phase2_blocked phase-2 blockers, $missing missing" >&2
    exit 1
fi
