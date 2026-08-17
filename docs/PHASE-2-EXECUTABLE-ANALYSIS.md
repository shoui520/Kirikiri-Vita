# Phase 2: executable analysis

## Status

Phase 2 is **deferred**. It remains on the project roadmap, but current work is
focused on general engine usability and compatibility. Nothing in the runtime
currently claims to recover a filter from a Windows executable.

Phase 1 and Phase 2 have different evidence boundaries:

- Phase 1 infers only what is completely constrained by XP3 archive metadata
  and encrypted samples.
- Phase 2 statically analyzes the executable supplied with that same game when
  Phase 1 proves that archive evidence is insufficient.

Phase 2 is not a title database, patch lookup, or permission to select the
closest known filter. It must derive every emitted value from the supplied
game and validate the result against that game's archives.

## Trust and compatibility boundary

The analyzer must never execute the supplied x86 executable, load a Windows
DLL, or run generated native code. Host and Vita builds use the same bounded,
portable parser and lifter. Unsupported or ambiguous machine code produces an
explicit `unsupported executable protection` result.

The Kirikiroid2 patch corpus may describe broad technique families during
design work, but it is not an oracle for Phase 2. Runtime and tests must not
contain or consult title names, executable hashes, per-title constants,
control blocks, filename dictionaries, or copied filter programs from that
corpus.

Generated artifacts are data, not trusted code. They must use a small checked
intermediate representation (IR), be serialized with an analyzer version and
input digests, and pass the same independent archive validation as a Phase-1
rule before installation.

## Required inputs and outputs

Inputs:

1. the executable selected by the normal full game scan;
2. the game's XP3 indices and bounded encrypted samples;
3. optional DLL/TPM files located in the supplied game directory; and
4. the Phase-1 diagnosis explaining which evidence is missing.

Successful output is a self-contained extraction package containing:

- a content transform expressed in checked portable IR;
- any required filename-to-storage-name transform or recovered mapping;
- range, prefix and hash-folding rules;
- an evidence report tying each value to an executable location or data-flow
  expression; and
- validation results from independent files, offsets and archive families.

The package may then be lowered to `xp3filter.tjs` or consumed by an equivalent
native interpreter. It must not contain x86 instructions.

## Analysis pipeline

### 1. Select and parse the PE image

Select the likely game executable using the existing `GameScanner` result,
then parse PE32/PE32+ headers, sections, RVA/file-offset mappings, imports,
exports, resources, relocations and overlay data. All counts, sizes and address
arithmetic are bounded before allocation.

The first implementation should target 32-bit x86 Kirikiri executables while
keeping the PE reader architecture-neutral. Imports such as `VirtualAlloc`,
`VirtualProtect` and `FlushInstructionCache`, plugin exports such as `V2Link`,
and Kirikiri RTTI/symbol strings are supporting evidence, never sufficient by
themselves to select a decoder.

### 2. Discover embedded modules

Search declared resources and recognized internal-module tables first. A
bounded fallback scanner may consider zlib streams only when adjacent size
metadata is internally consistent and the inflated bytes form a valid PE
image. Random `MZ` or zlib signatures are not accepted without structural
validation.

Standalone game-local DLL/TPM files pass through the same PE validator. Every
candidate records its source executable, file range, compression, digest and
validation reason.

### 3. Locate the archive filter and filename resolver

Build a conservative call graph from exports, registrations, RTTI references,
imports and constant/data xrefs. Recover both sides of protected storage:

- content decoding, which receives the XP3 hash, logical offset and byte span;
  and
- storage-name resolution, which may hash or map the requested logical
  filename before XP3 lookup.

These are separate requirements. A correct content filter cannot make a game
work when its archive index contains only opaque storage names.

### 4. Lift supported code into portable IR

Start with an x86 subset sufficient for observed Kirikiri protection modules:
integer moves, byte/word/dword loads and stores, bitwise operations, shifts,
rotates, add/subtract, multiply, comparisons and bounded conditional branches.
Calls must resolve to a recognized pure helper or fail analysis.

Self-generating decoders require two stages: lift or model the code generator,
then translate its emitted program to the same portable IR. Do not map the
generated buffer executable and do not emulate arbitrary Win32 behavior.

The IR needs explicit operations for:

- hash-derived scalar expressions;
- bounded prefix/range splitting;
- byte-wise and periodic transforms;
- finite literal/control-block reads;
- filename normalization and digest calculation; and
- conditional selection without unbounded loops.

Every load is bounds checked and every loop has a statically established
maximum. IR complexity and runtime work are capped.

### 5. Synthesize and validate

Run the recovered rule on bounded samples that were not used to recover it.
Validation must cover multiple archives, hashes, offsets on both sides of each
boundary, and complete small files where possible. Recognized headers, text
decoding and container structure must agree simultaneously.

For opaque filenames, first recover enough name resolution to select files by
logical name or to classify them independently. Guessing extensions from file
sizes or accepting a single plausible header is not validation.

Only a complete result may be cached. The cache key includes the executable
digest, archive fingerprint, analyzer/IR version and all contributing module
digests. Any mismatch invalidates it.

## DeepOne case study

DeepOne is the current Phase-2 development case. The following facts were
recovered directly from the installed game and its executable; they are not a
runtime signature or title lookup.

### Reproducible input facts

| Item | Observed value |
|---|---|
| Executable | `DeepOne.exe`, PE32 i386, 4,423,680 bytes, six sections |
| Executable SHA-256 | `eb0fe251dfa13103ae9a61d52d00f5b91b355f52452b2b431ddde52f36d6a5ff` |
| Phase-1 result | executable analysis required because archive filenames are obfuscated |
| `data.xp3` index | 4,188 entries; `startup.tjs` is visible but most entries use 32-hex-character storage names |
| `bgimage.xp3` index | 417 entries with the same opaque-name pattern |

At executable file offset `0x2f1148`, two little-endian sizes describe an
internal compressed module: `0x1ca00` bytes unpacked and `0xdc06` bytes
compressed. A zlib stream begins at `0x2f1150`. It inflates to a valid
117,248-byte PE32 DLL whose SHA-256 is
`b0fcdbc9375bfa92739e3b17cf0e503061b9cfbbfaa82ba21b69e1a84fb5347d`.
The module exports `V2Link`, `V2Unlink` and their stdcall aliases; its export
name is `cxdec_tpm.dll`, and its RTTI contains `CxFilterFS`, `CxFilterStream`,
`CxFilterDecrypt` and `CxdecFilterCore`.

### Confirmed decoder structure

The internal DLL's `.rdata` contains a 128-byte per-game control block at file
offset `0x13330` (RVA `0x14130`). Its SHA-256 is
`6c6c40b2fe837d6e00627ce46cd59f274cfec373ba4b10d2329b54b9dadff6d2`.
Native code returns this address to the generated-code builder; the analyzer
must recover the xref and data flow rather than searching for this digest.

The DLL has 128 logical hash-key slots backed by 100 reusable physical cache
entries; each generated routine occupies 128 bytes. The logical slot is derived
from the low seven hash bits, while generation/execution also uses the remaining
hash bits and their complement. The DLL allocates `0x3200` bytes of executable
memory and flushes the instruction cache on Windows. A portable implementation
must instead model this builder and lift its result to checked IR.

The content decoder constructor computes the split boundary as:

```text
(hash & 0x271) + 0x69b
```

The range dispatcher applies the original hash before that boundary and the
folded value `(hash >> 16) ^ hash` after it, including reads that cross the
boundary. The inner routine derives a small per-hash descriptor used for bulk
XOR plus exceptional byte positions. This structure is visible in native data
flow, but the generated program has not yet been lifted and validated as a
generic implementation.

Some manually inspected sample decodes produce recognizable TJS/text or sound
loop metadata, but this is preliminary evidence only. No Phase-2 rule currently
passes whole-file, cross-boundary and independent-archive validation, so no
DeepOne filter is accepted or shipped.

### Remaining DeepOne blockers

1. Parse the internal-module descriptor generically instead of using the known
   offset.
2. Recognize the cxdec registration/call graph without title or binary hashes.
3. Recover the 128-byte control-block xref and generated-code-builder inputs.
4. Lift the complete generated program and prove byte-for-byte behavior on
   independent samples and boundary crossings.
5. Recover the opaque storage-name resolver. Content decryption alone is not
   sufficient for the mostly digest-named XP3 indices.
6. Synthesize/cache a portable filter package and re-run Phase-1-strength
   validation over all relevant archives.
7. Separately audit the game's native-plugin requirements. Successful archive
   recovery does not imply that its animation, movie, image and UI plugins are
   supported by the Vita engine.

## Roadmap

Phase 2 resumes in the following order:

1. **PE foundation** — bounded PE/RVA reader with malformed-input tests and
   synthetic resources/overlays.
2. **Embedded-module extraction** — recognized internal-module descriptors,
   bounded zlib inflation and PE validation.
3. **Analysis IR** — checked expression/range/name-transform IR, interpreter,
   serializer and complexity limits.
4. **cxdec family support** — generic registration discovery, control-block
   recovery and generated-program lifting, using DeepOne only as a local
   integration case.
5. **Filename resolution** — recover the logical-name to opaque-storage-name
   path and validate real logical lookups.
6. **Host pipeline** — add `prepare-executable` after a Phase-1 escalation,
   emit an evidence report, validate archives and cache only complete results.
7. **On-device pipeline** — reuse the same parser and IR interpreter with
   streaming I/O and strict memory/time budgets; cache a successful result so
   analysis is a one-time first-launch operation.
8. **Compatibility handoff** — after decryption succeeds, inventory scripts,
   media and plugins as a separate engine-compatibility phase.

## Acceptance criteria

Phase 2 is complete for a protection family only when all of the following are
true:

- no title name, known patch, executable hash or per-title constant selects the
  result;
- changing or removing the relevant executable data causes analysis to fail;
- the emitted IR is deterministic and contains no native code;
- independent archive samples and exact boundary-crossing cases validate;
- opaque filenames, when present, resolve from real logical game names;
- malformed PE, zlib, control-block and generated-program inputs fail closed;
- host and Vita interpreters produce byte-identical output; and
- the filter package is invalidated by any executable/archive/analyzer-version
  change.

## On-device feasibility

On-device analysis is technically feasible for supported, statically
recognizable protection families. A 32-bit x86 executable can be parsed and
lifted on ARM without executing it, and the resulting compact IR can run in the
existing extraction-filter path. The work should be performed once, using
streamed reads and bounded scratch buffers, then cached under
`ux0:data/krkrvita`.

“Arbitrary executable” cannot mean arbitrary native behavior. Packers,
anti-analysis code, undocumented external services or an instruction pattern
outside the supported lifter must produce an explicit unsupported result. The
safe goal is automatic recovery from arbitrary *supplied games whose protection
falls within a verified analysis family*, with no title-specific knowledge.
