# Retail compatibility gate

The immutable retail corpus is declared in
`tests/retail_compatibility_manifest.txt`. The gate is fail-closed: decrypting
an XP3 or compiling `startup.tjs` is a prerequisite, not a compatibility
result.

For every Phase-1 title the host runner verifies the directory fingerprint and
expected heuristic evidence, opens every XP3 through the generated filter,
decodes every TJS/KAG source, compiles scripts and inline blocks with Yuri's
own TJS compiler, validates every supported image/audio payload signature,
checks loose and archived files for known unsupported runtime formats, and
compares literal plug-in requests with the same module inventory consumed by
the Vita sealed loader. For caught/conditional loads it also scans every
reachable boot source for the plug-in's script-visible API family (for example
`Motion.Player` after a caught `motionplayer.dll` load, or
`Scripts.getObjectCount` after `scriptsEx.dll`). This cross-file check prevents
an optional-looking missing DLL from hiding a boot-fatal missing global/class.
Empty sentinel assets, expression-storage TJS
units, dormant plug-ins, dormant compile findings and over-approximated
unresolved scenario edges are reported separately.

The manifest has three states:

- `phase1`: the strict host capability audit must pass.
- `runtime_blocked`: Phase 1 works, but an exact first unsupported runtime
  diagnostic must reproduce. This is a passing negative regression test, not a
  compatible title.
- `phase2`: Phase 1 must reproduce the pinned executable-analysis diagnosis.

`ctest --test-dir build-host -L compatibility --output-on-failure` verifies
both positive and negative regressions. The product gate is stricter:

```sh
./scripts/run-retail-compatibility.sh --require-all
cmake --build build-host --target krkrvita-final-compatibility-gate
```

It runs every row, aggregates every failure, and returns nonzero for either
blocked state. When a capability is implemented, its pinned negative test must
fail because the old diagnostic disappeared. Promote the manifest row to
`phase1` only after the complete static audit and a physical-Vita smoke run
both pass.

## Current 19-title result

The current static result is 17 compatible, 1 runtime-blocked and 1 Phase-2
blocked, with no missing paths or unexpected diagnostics. The runtime and
Phase-2 rows remain deliberate fail-closed regression gates until their
respective capabilities are implemented.

| State | Title | First pinned blocker |
| --- | --- | --- |
| compatible | 色情教団 | — |
| compatible | chichimiko | — |
| compatible | swap_re | — |
| compatible | 邪娠娼館 | — |
| compatible | PL-0002 | — |
| compatible | 通淫母 | — |
| runtime-blocked | こんそめ！～combination somebody～ | invalid decoded `system/rule20.png` payload |
| compatible | 悪役令嬢母娘の下僕になったので孕ませオナホに躾けて破滅ＥＮＤを回避する | — |
| compatible | 湯けむり | — |
| phase2 | DeepOne | obfuscated filenames require executable-derived constraints |
| compatible | LPK-30008 | — |
| compatible | 鳥籠のマリアージュ | — |
| compatible* | のーぶる☆わーくす | Motion, layerExDraw and scriptsEx script-surface fallbacks; physical animation smoke test still required |
| runtime-blocked | 車輪の国、向日葵の少女 | physical Vita: transparent title/message/UI layers render completely black |
| compatible | taimakenshi | — |
| compatible | 傲慢巨乳魔王ルシファー、追放された底辺召喚士の絶対服従孕ませ使い魔に堕ちる | — |
| compatible | 王女＆女騎士Ｗド下品露出 | — |
| compatible | ダメダメなボクに舞い降りた全肯定ママ女神！ | — |
| compatible | Moto Yankee Tsuma Hinako | — |

A row records the first blocker, not an assertion that no later blocker exists.
Fixing it intentionally advances the audit to the next unsupported dependency.
For example, a title can be playable while still failing this stronger product
gate because its opening movie has no Vita decoder.

`compatible*` means the strict host gate and boot API-surface checks pass, while
closed-source motion/GDI+ features are represented by documented script-surface
fallbacks. It is not evidence of pixel-identical E-mote or affine-plugin
rendering; a physical Vita smoke run remains required before upgrading that
claim.

### Virtual optical-media checks

Sharin no Kuni contains an explicit `Storages.searchCD("syarin")` guard in
`scenario/first.ks`; an empty result calls `kag.shutdown()` after displaying its
loading artwork. Its supplied `sharin.exe` differs from the original
`syarin.exe` by one five-byte relative jump which skips the same DVD branch.
Vita does not launch that Windows executable, so the generated Vita
`StorageImpl.cpp` implements the equivalent engine contract: any non-empty CD
label resolves to the selected, mounted project path, while an empty label
still returns “not found.” The `yuri-virtual-cd-search-satisfied` boot marker
and the `virtual_cd_is_present` host contract test keep this adaptation visible
and regression-tested.

This only clears the optical-media startup check. It does **not** make the
title compatible by itself. On the physical Vita, the game boots and advances
into the scenario, but transparent title-screen, message-box, and other UI
graphics render as solid black in the software-rendering configuration. The
August 2026 VPK retest confirmed that this remains unresolved.

An ARMv7 differential test found small destination-alpha rounding differences
between Yuri's selected ARM routines and its scalar TVPGL routines. The build
was changed to select the byte-exact scalar destination-additive-alpha routines,
and the Cortex-A9 differential then passed. The physical-Vita image remained
incorrect, however, so that discrepancy was real but was **not the cause of
this bug**. No compositor or presentation root cause has been established.
Passing the Phase 1 corpus row, generated-source contracts, Cortex-A9 arithmetic
probe, or disc-check test must therefore not be reported as a successful visual
runtime gate for this title.

## Cortex-A9 supplement

`scripts/run-retail-matrix-on-cortex-a9.sh` compiles and transfers a static
ARMv7 probe plus only the decoded `startup.tjs` of currently compatible rows.
It rejects XP3, EXE, DLL, media and save payloads, caps the bundle at 64 files
and 8 MiB, and deletes remote staging after the run. It is a CPU/ABI/compiler
supplement; it does not replace the full workstation corpus audit or a
physical-Vita runtime test. Vita3K is not used.

The current board run transferred 9 script probes in a 4,313-byte bundle and
compiled all nine successfully on a Cortex-A9.
