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

The manifest has four states:

- `phase1`: the strict host capability audit must pass. This is a *host* result.
  It is not evidence that the title runs on a Vita.
- `runtime_blocked`: Phase 1 works, but an exact first unsupported runtime
  diagnostic must reproduce. This is a passing negative regression test, not a
  compatible title.
- `phase2`: Phase 1 must reproduce the pinned executable-analysis diagnosis.
- `hardware_blocked`: the host audit passes and a recorded physical-Vita run
  failed. The row must have a matching `blocked` receipt in
  `tests/retail_hardware_evidence.txt`.

Physical results live in `tests/retail_hardware_evidence.txt`, keyed by game id
and VPK SHA-256. Nothing in the host gate runs on a Vita, so only a receipt can
support a compatibility claim, and a receipt only covers the exact VPK hash it
names. Rebuilding the VPK invalidates it.

`ctest --test-dir build-host -L compatibility --output-on-failure` verifies
both positive and negative regressions. The product gate is stricter:

```sh
./scripts/run-retail-compatibility.sh --require-all
cmake --build build-host --target krkrvita-final-compatibility-gate
```

It runs every row, aggregates every failure, and returns nonzero for any
blocked state. When a capability is implemented, its pinned negative test must
fail because the old diagnostic disappeared. Promote the manifest row to
`phase1` only after the complete static audit and a physical-Vita smoke run
both pass.

## Current 19-title result

The current result is 10 host-audit passed, 6 hardware verified, 1
hardware-blocked, 1 runtime-blocked and 1 Phase-2 blocked, with no missing
paths or unexpected diagnostics. The blocked rows are deliberate fail-closed
regression gates.

"Host-audit passed" is not "compatible". It means the static capability audit
found no blocker; it says nothing about the Vita renderer, plug-in behaviour,
storage timing or modal UI. Only a `passed` row in
`tests/retail_hardware_evidence.txt` earns "hardware verified", and that row
names the exact VPK hash it was observed on.

The `Id` column is the manifest id and is checked mechanically against
`tests/retail_compatibility_manifest.txt`, so this table cannot drift out of
agreement with the executable gate.

| State | Id | Title | First pinned blocker |
| --- | --- | --- | --- |
| hardware verified | seishoku | 色情教団 | — |
| host-audit passed | chichimiko | chichimiko | — |
| hardware verified | swap_re | swap_re | — |
| host-audit passed | jashin | 邪娠娼館 | — |
| host-audit passed | pl0002 | PL-0002 | — |
| hardware verified | kayoinbo | 通淫母 | — |
| runtime-blocked | consome | こんそめ！～combination somebody～ | invalid decoded `system/rule20.png` payload |
| host-audit passed | akuyaku | 悪役令嬢母娘の下僕になったので孕ませオナホに躾けて破滅ＥＮＤを回避する | — |
| hardware verified | yukemuri | 湯けむり | — |
| phase2 | deepone | DeepOne | obfuscated filenames require executable-derived constraints |
| host-audit passed | lpk30008 | LPK-30008 | — |
| host-audit passed | torikago | 鳥籠のマリアージュ | — |
| hardware verified | nobleworks | のーぶる☆わーくす | — |
| hardware-blocked | sharin | 車輪の国、向日葵の少女 | physical Vita: transparent title/message/UI layers render solid black |
| host-audit passed | taimakenshi | taimakenshi | — |
| host-audit passed | lucifer | 傲慢巨乳魔王ルシファー、追放された底辺召喚士の絶対服従孕ませ使い魔に堕ちる | — |
| host-audit passed | oujo_wkishi | 王女＆女騎士Ｗド下品露出 | — |
| hardware verified | allokmama | ダメダメなボクに舞い降りた全肯定ママ女神！ | — |
| host-audit passed | hinako | Moto Yankee Tsuma Hinako | — |

A row records the first blocker, not an assertion that no later blocker exists.
Fixing it intentionally advances the audit to the next unsupported dependency.
For example, a title can be playable while still failing this stronger product
gate because its opening movie has no Vita decoder.

### Noble Works

The 2026-08-17 device log settled this. The title screen, New Game and script
loading all work; the run dies at `start.ks(9)` `[scenestart]`, in
`KAGEnvPlayer.restore()` at `env.onRestore((new PSBFile(obj)).root)`, with
`Cannot parse PSB: PSB table offset is out of range`. The "black screen" is the
fatal-error dialog left over an unpainted frame.

The cause was in our PSB parser, not the data. It required every header offset
to satisfy `offset < size`. That is right for the five *tables*, which begin
with a type byte, but wrong for the two payload *bases* (`offsetStringsData`,
`offsetChunkData`): an empty payload region legitimately begins one past the
last byte. Every Noble Works scene state embeds no resources, so
`offsetChunkData == size` and all of them were rejected.

The host tests missed it because `tests/test_psb.cpp` only read `scene.sdb`'s
`data` column. The blobs the game actually parses are `text.state` in
`scenedata.sdb` — 57,691 of them, none of which were covered, and **all** of
which failed. That coverage now exists, along with assertions on both sides of
the boundary.

Independently of that fix, `motionplayer` and `layerExDraw` remain
`control_flow_fallback` modules, and `layerExRaster.dll`, `layerExBtoA.dll` and
`layerExSubImage.dll` are not in the registry at all. Those three links are
guarded by `try`/`catch` behind a `typeof` feature test, so their absence costs
the optional effect and nothing else. Noble Works also ships `nobleworks.tpm`,
`yuzuex.dll` and `kagexopt.dll`, which the sealed registry does not implement,
and `Storages.getLastModifiedFileTime` is missing, which throws once per
transition from `kagenvironment.tjs(619)`.

Real portable implementations of `layerExDraw`, `layerExRaster`, `layerExBTOA`
and `windowEx` exist in the KrKr2-Next tree. `layerExDraw`'s non-Windows
backend depends on libgdiplus (glib/cairo/pango), so adopting it on Vita is a
port in its own right rather than a drop-in.

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

### What has been eliminated

The TVPGL blend arithmetic is **not** the cause, and that is now settled with
hardware evidence rather than inference.

`tests/test_yuri_arm_alpha.cpp` previously compared only the `_HDA`, `_a` and
`_ao` blend variants, and it was referenced by no build file, so it never ran.
But `TVP_BLEND_4` selects the *plain*, non-HDA function whenever the
destination layer is opaque — which is exactly how an `ltAddAlpha` message
layer reaches the primary layer. The functions Sharin actually goes through had
never been compared at all.

They are now, via `scripts/run-arm-alpha-probe.sh`, which builds a static
ARMv7 binary and runs it on the Cortex-A9 board. Result: for
`TVPAdditiveAlphaBlend`, `TVPAdditiveAlphaBlend_o`, `TVPAlphaBlend` and
`TVPAlphaBlend_o`, **every colour channel matches scalar TVPGL exactly**. The
only differences are in the alpha byte, which is the documented non-HDA
contract — those variants do not hold destination alpha.

That alpha byte cannot reach the screen either: the main game frame is
presented by `present_bound_texture()` with `GL_REPLACE` and `GL_BLEND`
disabled, so framebuffer alpha is never sampled. (The `GL_BLEND` enable in
`vitagl_presenter.cpp` belongs to the movie-overlay path.)

Also checked and intact: the layer-type to blend-op mapping in `LayerIntf.cpp`
(`ltAddAlpha` → `omAddAlpha` → `bmAddAlpha`) and the `LayerBitmapIntf.cpp`
dispatch behind it.

Known about the title: its message layer is `ltAddAlpha` with
`frameColor = 0x000000` and `frameOpacity = 128`, so a *correct* message box
here is a 50%-transparent black rectangle. "Solid black" is therefore
consistent with opacity being lost somewhere after the blend functions, which
is where the next attempt should look.

An earlier ARMv7 differential found small destination-alpha rounding differences
between Yuri's selected ARM routines and its scalar TVPGL routines. The build
was changed to select the byte-exact scalar destination-additive-alpha routines,
and the Cortex-A9 differential then passed. The physical-Vita image remained
incorrect, however, so that discrepancy was real but was **not the cause of
this bug**. No compositor or presentation root cause has been established.
Passing the Phase 1 corpus row, generated-source contracts, Cortex-A9 arithmetic
probe, or disc-check test must therefore not be reported as a successful visual
runtime gate for this title.

## Cortex-A9 supplement

**`scripts/run-retail-matrix-on-cortex-a9.sh` must not be run as it stands.**
It still calls `scripts/run-cortex-a9-compatibility.sh`, the VitaSX2-derived
wrapper the project prohibits. The supported helper is
`scripts/run-cortex-a9-board.sh`, which takes one already-built static ARMv7
binary, runs it, and removes the remote staging directory.

The matrix script otherwise compiles and transfers a static
ARMv7 probe plus only the decoded `startup.tjs` of host-audit-passed rows.
It rejects XP3, EXE, DLL, media and save payloads, caps the bundle at 64 files
and 8 MiB, and deletes remote staging after the run. It is a CPU/ABI/compiler
supplement; it does not replace the full workstation corpus audit or a
physical-Vita runtime test. Vita3K is not used.

The current board run transferred 9 script probes in a 4,313-byte bundle and
compiled all nine successfully on a Cortex-A9.
