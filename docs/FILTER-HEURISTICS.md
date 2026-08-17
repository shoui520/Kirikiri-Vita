# XP3 filter inference

Automatic extraction-filter recovery has two deliberately separate phases.

## Phase 1: archive-only inference

Phase 1 may inspect XP3 indices, filenames, hashes, segment offsets and bounded
encrypted file samples. It must not inspect the Windows executable, identify a
title, import a known patch, or consume a game-local hand-written filter. The
explicit `prepare-heuristic` command does not initialize the patch repository
or perform network access. A result is accepted only when one synthesized
rule explains independent samples and the decoded bytes pass format/text
validation. Otherwise the result states either that archive evidence is
insufficient or that executable analysis is required.

The external Kirikiroid2 patch corpus was audited at commit
`0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300`. The audit tool found 326 extraction
filters, 261 exact source groups and 228 literal-independent structural groups.
No corpus file is linked, copied, packaged or consulted at runtime. Reproduce
the inventory with:

```sh
python3 scripts/audit-kirikiroid2-patches.py \
  /path/to/Kirikiroid2_patch --expect-count 326 --json /tmp/filter-audit.json
```

The corpus techniques reduce to these behavioral families:

| Family | Phase-1 treatment |
|---|---|
| Identity/plain archive | Validate without generating a destructive transform. |
| Uniform XOR | Infer constants, any hash right shift, negation, and a constant combined with a shifted hash. |
| Hash folding | Infer XOR combinations of the four hash bytes and the remaining constant. |
| Position-derived XOR | Try absolute-offset addition, hash/offset parity, position-selected hash shifts and periodic hash-byte lanes. |
| Rotating hash stream | Recover the 31-bit seed XOR from known bytes and validate 29/31/32-byte schedules. |
| Deterministic PRNG stream | Try the observed hash-seeded 512-byte LCG schedule directly. |
| Literal periodic stream | Synthesize a table only when every phase is independently constrained; never fill unknown phases by repetition. |
| Prefix/range transform | Infer a bounded plaintext prefix, transformation start, or independently constrained header endpoint. More complex boundaries require whole-structure validation. |
| Filename/extension branch | Infer extension groups and top-level path groups independently, then emit one case-insensitive guarded filter only if every observed group is accounted for. |
| Byte post-transform | Try XOR followed by addition, nibble exchange, and the parameter-free popcount rotation. |
| Sparse/header repair | Try only when the target format gives enough structural constraints for every changed position; otherwise defer. |
| Hash dictionaries and large literal tables | Defer unless archive constraints recover every referenced value. |
| `cxdec`/generated-code VM | Always phase 2. Archive headers do not contain the per-game control block or generated program. |

Phase 1 samples up to 4 KiB from a diverse, round-robin set of strong file
types rather than trusting the first adjacent entries in archive order. PNG,
JPEG, Ogg, RIFF/WAVE, WebP, BMP, MP3, MPEG, ASF/WMV, PSB, TLG and Kirikiri script signatures are
recognized. Kirikiri scripts are validated in compiled FE-FE form and as
plain Shift-JIS/ASCII, UTF-8-BOM, or UTF-16 source; a source script is never
forced to use the compiled marker. Candidate complexity is penalized so a
large table cannot win over an equally explanatory small expression.

“Header decoded” is not synonymous with “filter recovered.” Unknown table
phases, filename branches, later ranges and sparse repairs make a candidate
incomplete; those cases must be rejected or escalated instead of producing a
plausible but corrupt `xp3filter.tjs`.

## Phase 2: executable analysis

Phase 2 is a separate, currently deferred subsystem. It analyzes the supplied
game's own Windows EXE: imports, resources, embedded modules, code and relevant
call sites. Its job is to recover control blocks, generated operations, hash
dictionaries, literal tables, filename mappings and boundaries that Phase 1
proves are not identifiable from archive evidence. Its input is the game
itself, never a title-to-patch lookup. The complete design, DeepOne evidence,
safety boundary and implementation order are recorded in
[`PHASE-2-EXECUTABLE-ANALYSIS.md`](PHASE-2-EXECUTABLE-ANALYSIS.md).

The phase-1 CLI makes this boundary visible:

```text
filter: unknown
phase: 2
reason: recognizable archive types are present, but no complete archive-only rule validates
```

This is a successful diagnosis, not permission to guess a filter.
