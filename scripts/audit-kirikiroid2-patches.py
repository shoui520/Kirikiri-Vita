#!/usr/bin/env python3
"""Inventory extraction-filter techniques in an external Kirikiroid2 corpus.

This is a development/audit tool, not a runtime database.  It deliberately
does not emit game names, constants, tables, or filter source into krkrvita.
Its purpose is to make the phase-1 capability boundary mechanically
reviewable: every xp3filter.tjs is assigned behavioral features and either an
archive-inference family or a reason that executable analysis is required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
import re
import sys


def compact_source(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\r\n]*", "", text)
    return re.sub(r"\s+", "", text)


def structural_fingerprint(text: str) -> str:
    """Fingerprint control flow without retaining title-specific literals."""
    value = compact_source(text)
    value = re.sub(r'"(?:\\.|[^"\\])*"', '"S"', value)
    value = re.sub(r"'(?:\\.|[^'\\])*'", "'S'", value)
    value = re.sub(r"<%[0-9a-fA-F]*%>", "<%B%>", value)
    value = re.sub(r"\b(?:0x[0-9a-fA-F]+|\d+)\b", "N", value)
    return hashlib.sha256(value.encode("ascii", "replace")).hexdigest()[:16]


def has(pattern: str, source: str) -> bool:
    return re.search(pattern, source, flags=re.I | re.S) is not None


def classify(text: str) -> tuple[str, list[str], str]:
    s = compact_source(text)
    features: set[str] = set()

    opaque_vm = has(r"EncryptionControlBlock|xcode_op_|cxdec", s)
    huge_lookup = len(s) >= 8_000 and ("%[" in s or "<%" in s)

    if has(r"\.xor\(", s):
        features.add("bulk-xor")
    if has(r"\.add\(", s) or has(r"(?:b\[[^]]+\]|\bc\b)\s*\+=", s):
        features.add("byte-add")
    if has(r"(?:b\[[^]]+\]|\bc\b)\s*-=", s):
        features.add("byte-subtract")
    if has(r"b\[[^]]+\]\s*\^=", s):
        features.add("indexed-xor")
    if ("<<" in s and ">>" in s and "|" in s):
        features.add("bit-rotation")
    if has(r">>4\)\|\([^;]+<<4", s):
        features.add("nibble-swap")
    if s.lower().count("filename") > 1 or "substr(" in s or ".length" in s:
        features.add("filename-branch")
    if has(r"\.(?:tlg|png|ogg|tjs|ks|asd|mpg|wav|jpg|jpeg)['\"]|\bextension\b|\bext\b", s):
        features.add("extension-branch")
    if has(r"offset|\bo\b|\boff\b", s):
        features.add("absolute-offset")
    if has(r"(?:start|skip|range|off)\s*=|if\([^)]*(?:offset|\bo\b)[^)]*[<>]=?", s):
        features.add("range-limit")
    if "fixTLG" in s or "TLGHdr" in s:
        features.add("header-repair")
    if has(r"loff\s*=|switch\([^)]*(?:off|offset)|case\d+:", s):
        features.add("sparse-or-segmented")
    if has(r"0x15A4E35|1103515245|1664525|214013", s):
        features.add("prng-stream")
    if has(r"\[(?:hash|h)>>\d+(?:,(?:hash|h)>>\d+){1,}", s):
        features.add("hash-byte-lanes")
    if has(r"(?:hash|h)\s*>>\s*\([^)]*(?:offset|\bo\b)", s):
        features.add("position-selected-hash-shift")
    if ((">>8" in s or ">>16" in s or ">>24" in s) and
            ("hash" in s or re.search(r"\bh\b", s)) and
            ("^" in s or "+" in s)):
        features.add("hash-fold")
    if has(r"(?:&31|%31|&0x1[fF]|%0x1[fF]|%29)", s) and has(r"(?:<<23|>>8)", s):
        features.add("rotating-hash-stream")
    if has(r"(?:const\))?\[[^]]{40,}\]|<%[0-9a-f]{40,}%>", s):
        features.add("literal-byte-table")
    if has(r"%\[[^]]*=>", s):
        features.add("hash-dictionary")
    if has(r"\^\s*0x[0-9a-f]+", s):
        features.add("per-game-seed")
    if has(r"if\([^)]*(?:&1|%2)", s):
        features.add("parity-branch")

    if opaque_vm:
        features.add("generated-code-vm")
        primary = "opaque-generated-code"
        disposition = "requires-executable-analysis"
    elif huge_lookup:
        primary = "opaque-hash-or-byte-table"
        disposition = "requires-executable-analysis"
    elif "prng-stream" in features:
        primary = "deterministic-prng-stream"
        disposition = "archive-inference-candidate"
    elif "hash-dictionary" in features:
        primary = "hash-specific-dictionary"
        disposition = "requires-executable-analysis"
    elif "sparse-or-segmented" in features:
        primary = "sparse-or-segmented-transform"
        disposition = "archive-inference-candidate"
    elif "extension-branch" in features or "filename-branch" in features:
        primary = "filename-conditioned-transform"
        disposition = "archive-inference-candidate"
    elif "rotating-hash-stream" in features:
        primary = "rotating-hash-stream"
        disposition = "archive-inference-candidate"
    elif "literal-byte-table" in features:
        primary = "periodic-literal-table"
        disposition = "archive-inference-candidate"
    elif "range-limit" in features or "header-repair" in features:
        primary = "range-or-header-transform"
        disposition = "archive-inference-candidate"
    elif "bit-rotation" in features or "nibble-swap" in features or \
            "byte-add" in features or "byte-subtract" in features:
        primary = "byte-post-transform"
        disposition = "archive-inference-candidate"
    elif "position-selected-hash-shift" in features or "parity-branch" in features or \
            ("absolute-offset" in features and "indexed-xor" in features):
        primary = "position-dependent-xor"
        disposition = "archive-inference-candidate"
    elif "hash-fold" in features:
        primary = "hash-fold-xor"
        disposition = "archive-inference-candidate"
    elif "bulk-xor" in features or "indexed-xor" in features:
        primary = "uniform-or-simple-xor"
        disposition = "archive-inference-candidate"
    else:
        primary = "unclassified"
        disposition = "unclassified"

    return primary, sorted(features), disposition


def phase1_strategy(primary: str, features: list[str]) -> str:
    if primary in {"opaque-generated-code", "opaque-hash-or-byte-table",
                   "hash-specific-dictionary"}:
        return "phase2-executable-state"
    if primary == "periodic-literal-table" and "literal-byte-table" in features:
        return "bounded-period-synthesis-or-phase2"
    if primary == "sparse-or-segmented-transform":
        return "structure-validated-staged-synthesis-or-phase2"
    if primary == "range-or-header-transform":
        return "structure-validated-range-synthesis-or-phase2"
    return "phase1-direct-model"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--expect-count", type=int)
    args = parser.parse_args()

    files = sorted(args.corpus.rglob("xp3filter.tjs"))
    if args.expect_count is not None and len(files) != args.expect_count:
        print(f"expected {args.expect_count} filters, found {len(files)}", file=sys.stderr)
        return 2
    records = []
    for path in files:
        raw = path.read_bytes()
        text = raw.decode("ascii", "replace")
        primary, features, disposition = classify(text)
        records.append({
            "path_digest": hashlib.sha256(
                str(path.relative_to(args.corpus)).encode("utf-8")
            ).hexdigest()[:16],
            "source_digest": hashlib.sha256(raw).hexdigest(),
            "structural_group": structural_fingerprint(text),
            "bytes": len(raw),
            "primary_family": primary,
            "features": features,
            "disposition": disposition,
            "phase1_strategy": phase1_strategy(primary, features),
        })

    primary_counts = Counter(r["primary_family"] for r in records)
    disposition_counts = Counter(r["disposition"] for r in records)
    feature_counts = Counter(f for r in records for f in r["features"])
    strategy_counts = Counter(r["phase1_strategy"] for r in records)
    report = {
        "filter_count": len(records),
        "exact_source_groups": len({r["source_digest"] for r in records}),
        "structural_groups": len({r["structural_group"] for r in records}),
        "primary_families": dict(sorted(primary_counts.items())),
        "dispositions": dict(sorted(disposition_counts.items())),
        "features": dict(sorted(feature_counts.items())),
        "phase1_strategies": dict(sorted(strategy_counts.items())),
        "filters": records,
    }

    print(f"filters: {report['filter_count']}")
    print(f"exact source groups: {report['exact_source_groups']}")
    print(f"structural groups: {report['structural_groups']}")
    for name, count in report["primary_families"].items():
        print(f"family {name}: {count}")
    for name, count in report["dispositions"].items():
        print(f"disposition {name}: {count}")
    for name, count in report["phase1_strategies"].items():
        print(f"strategy {name}: {count}")

    unknown = primary_counts.get("unclassified", 0)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if unknown:
        print(f"unclassified filters: {unknown}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
