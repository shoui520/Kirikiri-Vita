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
| Offset-segmented key change | Recover a fixed byte stride and a per-region transform when one transform explains the head of a file and nothing explains the rest. See below. |
| Hash-multiplied key | Infer `(hash * m) & 0xff` for any multiplier, in XOR and in subtractive form, so a filter that adds on write rather than XORing is recoverable. |
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

That distinction is enforced rather than merely stated. Every candidate is
scored on how much of each decoded sample still agrees with its own format
past the signature: PNG chunk checksums, Ogg page checksums, and how far a
script stays readable. A rule that reproduces a file's magic bytes and then
produces noise is penalised in proportion to what it leaves unexplained, so it
cannot outrank a rule that decodes the whole sample. `krkrvita-tool
xp3-diagnose` prints this agreement per sample.

### Offset-segmented rules

Some filters do not apply one transform to a whole file. They change key after
a fixed number of bytes — most simply by encrypting a header and leaving the
body alone, and in the general case by walking a short sequence of per-region
keys before settling on one for the remainder.

A whole-file search cannot see this: it finds the transform that satisfies the
format signature at offset 0 and gives up on the rest. Recovery therefore runs
as a second pass, seeded by the candidates already proven against every known
signature byte:

1. Textual samples that the head transform decodes correctly are the probes.
   Text is the only payload whose validity can be judged byte by byte at an
   arbitrary offset. At least two are required.
2. The stride is where the head transform stops producing readable text,
   taken as the minimum across probes and refined downwards within a bounded
   window if the regions that follow do not resolve.
3. Each following region is solved against the probes over a small primitive
   set — identity, constant XOR, constant subtraction, and hash-multiplied XOR
   or subtraction — and the cheapest transform that keeps every probe readable
   for the whole region wins.
4. Regions are walked until one repeats the previous transform, which is how
   the filter says it has settled; that transform then covers the rest of the
   file. A sequence that never repeats inside the segment cap is rejected.
5. The composed rule is re-validated against *every* sample, including the
   binary ones the text search never consulted, and must beat the best
   whole-file rule outright rather than merely tie. A segmented rule is a
   larger hypothesis and pays for every region it introduces.

Dieselmine's Violated Hero III is the worked example. Archive evidence alone
recovers a 123-byte stride with four regions — `hash*21` XOR, `hash*32`
subtract, `hash*43` XOR, then `hash*54` subtract for the remainder — and the
result verifies against every one of the archive's 6,402 entries.

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
