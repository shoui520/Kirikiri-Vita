# Kirikiri Vita agent handoff — 2026-08-17

This document is the authoritative handoff for the next agent. The prior
conversation became extremely long and its intermediate claims are not all
reliable. When this document conflicts with an earlier optimistic statement,
prefer the physical-hardware results and the limitations recorded here.

## 1. Repository, branch, and non-negotiable working rules

- Work only in `/home/shoui/krkrvita`.
- The shell environment may start in `/home/shoui/krkrv`; that is the wrong
  directory. Always pass `/home/shoui/krkrvita` as the working directory or
  change to it explicitly before inspecting or building.
- Current branch: `main`.
- Current base commit: `09ab6409507da07e41ff9485398734050fff26f7`.
- The worktree is extremely dirty. It contains a large amount of valuable,
  mostly uncommitted compatibility work. Treat every existing modification and
  untracked file as user-owned.
- Never run `git reset --hard`, `git checkout --`, `git clean`, or any other
  cleanup/revert operation. Do not delete build trees or caches unless the user
  explicitly authorizes an exact target.
- Use `apply_patch` for edits.
- Never use Vita3K. Physical Vita output is the runtime authority.
- Never inspect, edit, or run anything from the VitaSX2 repository. In
  particular, do not use `scripts/run-cortex-a9-compatibility.sh`; it was based
  on a VitaSX2-specific wrapper and the user explicitly prohibited its use.
- The project-local safe board staging helper is
  `scripts/run-cortex-a9-board.sh`. It accepts one already-built static ARMv7
  binary, copies only that binary into a volatile directory, runs it, and
  removes the remote staging directory.
- Do not transfer complete games, XP3 archives, DLLs, media, or save data to the
  Cortex-A9 Linux board. The board has limited storage. Cortex-A9 results are a
  CPU/ABI supplement, not a Vita runtime substitute.
- Never run broad `rg` searches. Every `rg` invocation must name a tightly
  bounded file or small directory set and should use `--max-count`. Prefer
  direct `sed` ranges when the relevant file is known.
- The user permits builds with 12 parallel jobs (`-j12` or
  `--parallel 12`).
- The product uses Yuri's software rendering path. VitaGL is disabled for the
  real product because it has too many bugs. Do not treat a VitaGL-only fix,
  probe, or rendering result as relevant proof.
- Check existing Kirikiri implementations before inventing replacements. The
  user explicitly named these local reference trees:
  - `~/Kirikiroid2Yuri`
  - `~/Kirikiroid2-debloated`
  - `~/KrKr2-Next`
  - `~/krkr2`
  - `~/krkrsdl2`
- Kirikiroid2Yuri/Yuri is the primary behavioral oracle. Reuse authoritative
  implementations where possible. Kirikiri SDL2 is a source of individually
  reviewed portable fixes, not the product engine contract.
- For Vita platform APIs, consult the local Sony/PSP2SDK documentation under
  `/mnt/c/PATH/sony-psp2sdk` before guessing.
- Preserve copyright boundaries. Some plugins are closed source; a script
  stub, crossfade substitution, or no-op must be described honestly as a
  fallback, never as a faithful implementation.

## 2. Immediate user-reported truth: Noble Works is still broken

### Critical project-level warning: the host compatibility gate is not adequate

The project does **not** currently have proper host-side testing infrastructure
that can prove whether a game will or will not work on Vita. This is not a
minor caveat. It is a central unresolved engineering problem and must be fixed
alongside the individual game blockers.

The existing host audit is useful for catching deterministic archive,
decryption, script-compilation, payload-signature, and known-plugin problems.
It is nevertheless far too naive to support a product-level `compatible`
claim. It has already produced false confidence: games marked positive by the
host manifest can reveal new fatal exceptions, missing plugin semantics,
modal error windows, black screens, broken transparency, or no-op rendering
only when exercised on real hardware. Noble Works is the clearest current
example, and Sharin no Kuni is another.

In particular, the present host infrastructure does not execute enough of each
game's real startup and interactive scenario path. It often verifies that a
name exists or a script compiles without verifying that the implementation has
the required behavior. It treats some control-flow/no-op plugin fallbacks as
supported, does not exercise the sealed Vita plugin loader exactly as hardware
does, does not validate end-to-end PSB/SQLite/script/render behavior, and does
not reproduce the Vita software compositor, modal UI, storage timing, or
memory constraints. Consequently:

- a host `phase1` result is **not** proof that a game works;
- a green CTest run is **not** proof that a game works;
- successful plugin inventory matching is **not** proof that the plugin's
  required semantics exist;
- successful parsing is **not** proof that the parsed content can be consumed
  and rendered;
- successful VPK compilation is **not** a compatibility result;
- absence of a host diagnostic does **not** mean absence of a Vita blocker.

The next agent must not merely add Noble Works-specific exceptions and leave
this gap intact. The validation system needs deeper, game-path-aware tests and
a first-class physical-Vita evidence layer. Until then, all positive host rows
must be described as “host audit passed” or “statically compatible,” never as
proof that the title works. A title is product-compatible only after a
physical-Vita smoke run of the exact VPK hash exercises the relevant path.

Game:

`/mnt/j/YuzuSoft/のーぶる☆わーくす`

The latest VPK did **not** fix Noble Works. The current physical-Vita result is:

- the game presents a black screen;
- a log/inform window opens;
- the overall title is not usable;
- therefore Noble Works is **runtime-blocked on physical Vita**;
- it must not be described as `compatible`, `compatible*`, fixed, or hardware
  validated.

This specifically invalidates the optimistic row in
`docs/RETAIL-COMPATIBILITY-GATE.md`, which currently says:

`compatible* | のーぶる☆わーくす | Motion, layerExDraw and scriptsEx script-surface fallbacks; physical animation smoke test still required`

It also exposes a real validation-infrastructure flaw: the manifest still has
Noble Works as a positive `phase1` row even though the physical runtime is
broken. Static Phase 1 success currently proves archive/filter/script/API
surface coverage only. It does not prove that the game will display or run on
Vita.

Do not interpret the most recent PSB/short-read work as a Noble Works runtime
fix. It is a defensible engine correction and passes host tests, but the user
has now tested the resulting VPK and confirmed that the actual black-screen
problem remains.

## 3. Noble Works failure chronology

Keep these stages distinct; each exposed the next problem.

### 3.1 Original startup failure: missing Motion global

The first hardware log showed:

```text
Member "Motion" does not exist
@line(20) motion.tjs
mainwindow.tjs(1194)[(function) KAGWindow] <-- initialize.tjs(396)[(top level script) global]
```

Noble Works loads `motionplayer.dll` in a caught `Plugins.link`, but then uses
`Motion.ResourceManager` immediately. Treating the DLL as optional was wrong.

A script-surface fallback was added in:

- `include/krkrvita/motionplayer_surface.hpp`
- `src/engine/retail/yuri_motionplayer_module.cpp`

It supplies `Motion.ResourceManager`, `Motion.Player`, and
`Motion.SeparateLayerAdaptor`. It is intentionally not a PSB/E-mote renderer.
Important methods such as `Motion.Player.draw()` are no-ops. This removed the
immediate missing-global failure but did not establish visual compatibility.

### 3.2 Next hardware result: layerExDraw still failed to link

The next log repeatedly showed caught plugin exceptions including:

```text
Cannot load Plugin layerExDraw.dll
```

The most important occurrence is `custom.tjs(57)`:

```tjs
try { Plugins.link("layerExDraw.dll"); }
catch(e) { System.inform(e.message); }
```

This is not a silent optional failure. It explicitly opens an inform/log
window. The user's current phrase, “black screen-but-log-window-opens,” is
consistent with this exact path and must be investigated first.

Other caught/missing Noble Works plugins in the supplied logs include:

- `scriptsEx.dll`
- `layerExRaster.dll`
- `layerExDraw.dll`
- `layerExBtoA.dll`
- `motionplayer.dll` (the missing Motion failure was later bypassed)
- `windowEx.dll`
- `layerExSubImage.dll`

The tree now has script-surface modules for only some of these:

- `motionplayer.dll`: control-flow surface, no real motion pixels;
- `layerExDraw.dll`: GDI+/Layer method surface, many operations are no-ops;
- `scriptsEx.dll`: small introspection surface, several methods are conservative
  placeholders.

Relevant files:

- `include/krkrvita/yuri_plugin_capabilities.hpp`
- `include/krkrvita/motionplayer_surface.hpp`
- `include/krkrvita/layerexdraw_surface.hpp`
- `include/krkrvita/scriptsex_surface.hpp`
- `src/engine/retail/yuri_motionplayer_module.cpp`
- `src/engine/retail/yuri_layerexdraw_module.cpp`
- `src/engine/retail/yuri_scriptsex_module.cpp`
- `cmake/YuriBackend.cmake` around the plugin source list near line 4770
- `cmake/VerifyYuriBuild.cmake` around the plugin contracts near line 1320

There is a crucial contradiction to resolve:

- `include/krkrvita/yuri_plugin_capabilities.hpp` declares
  `layerexdraw.dll` and `scriptsex.dll` as supported internal modules;
- build verification checks that their source is compiled and their marker
  strings exist;
- physical logs still show `Plugins.link("layerExDraw.dll")` throwing.

Compilation, string presence, and a host-executed TJS surface do not prove that
the sealed Vita plugin registry actually accepts the module name at runtime.
Trace the ncbind registration/link path on the actual Yuri backend. Add narrow,
rate-limited hardware markers around registry collection and module lookup if
needed. Do not merely suppress the exception: even a successful link would
still leave most `layerExDraw` pixel operations as no-ops.

### 3.3 Next failure after reaching title: PSB scene parse error

At one point Noble Works reached the title screen, but choosing New Game failed
at `start.ks` line 9:

```text
Cannot parse PSB: PSB table offset is out of range
```

The host investigation established:

- `scenario.xp3` contains `scene.sdb`, an SQLite database;
- it has 437 non-null PSB/MDF scene blobs;
- all 437 blobs parse successfully with the current strict host parser;
- a representative row is MDF/zlib-wrapped PSB version 2;
- `video.xp3/drop.psb` also parses and has a substantial object/binary tree;
- Yuri's XP3 reader had single-read assumptions in paths where Vita may return
  a positive short read.

The following correction was added:

- `include/krkrvita/read_all.hpp`:
  - `read_all_bytes()` joins bounded positive partial reads;
  - `read_all_stream()` adapts that behavior to Yuri streams.
- `cmake/YuriBackend.cmake` generates a Vita-only `XP3Archive.cpp` overlay:
  - compressed segment reads join short reads;
  - uncompressed payload reads join short reads;
  - incomplete payloads fail instead of exposing an uninitialized/truncated
    tail.
- the generated Vita `StorageImpl.cpp` local-file read path also uses the
  bounded join.
- `cmake/VerifyYuriBuild.cmake` requires the generated short-read calls and
  forbids the old raw single-read forms.
- `tests/test_psb.cpp` includes an adversarial two-byte-at-a-time stream test
  and parses all installed Noble Works scene blobs when the game exists.

This patch is still worthwhile and should not be reverted casually. However,
its validation establishes correct reads and PSB parsing on the host. It does
not prove Noble Works's complete PSB-to-TJS-to-render path on Vita, and the
latest hardware result remains broken.

Potential later PSB/API issue to verify: `yuri_psbfile_module.cpp` currently
constructs a native `PSBFile` object exposing a `root` property. Confirm the
exact original/Kirikiroid2 `psbfile.dll` object contract. Noble's scripts may
expect direct property behavior or another conversion contract, not merely
successful binary parsing. The current test primarily validates the parser,
not the full native TJS API under the game's scene player.

## 4. Latest VPK and validation evidence

The latest built artifact at handoff time is:

`/home/shoui/krkrvita/build-vita-yuri/krkrvita-yuri.vpk`

Metadata:

- modification time: `2026-08-16 10:39:17 +0100`
- size: `31,639,595` bytes
- SHA-256:
  `a75c3942825fdd745bc9b6e834ce8415a817c142dc41e5c0c409508e76c6b87a`

Associated SELF:

- `/home/shoui/krkrvita/build-vita-yuri/krkrvita-yuri.self`
- modification time: `2026-08-16 10:39:15 +0100`
- size: `10,069,170` bytes

Validation completed before the user's latest hardware rejection:

- full host CTest: `40/40` passed;
- focused PSB tests passed;
- all 437 installed Noble Works scene PSB blobs parsed on host;
- Yuri generated-source verifier passed;
- Vita backend compiled;
- VPK packaging succeeded;
- `git diff --check` was clean before this handoff file was added.

What this does **not** mean:

- it does not mean Noble Works is fixed;
- it does not mean internal plugin names linked on physical Vita;
- it does not mean no-op plugin surfaces are semantically adequate;
- it does not mean PSB scenes render;
- it does not mean a title/menu/new-game/input/save/load/audio/visual smoke run
  passed;
- it does not override the user's physical-Vita observation.

## 5. Current compatibility-gate corpus

The user-defined 19-title product gate is:

1. `/home/shoui/Agents/CodexMax/色情教団`
2. `/mnt/j/rapapuru/chichimiko`
3. `/mnt/j/ORCSOFT/swap_re`
4. `/mnt/j/ORCSOFT/邪娠娼館`
5. `/mnt/j/PL-0002`
6. `/mnt/j/KAYOINBO/通淫母`
7. `/mnt/j/SilverBullet/こんそめ！～combination somebody～`
8. `/mnt/j/miel/悪役令嬢母娘の下僕になったので孕ませオナホに躾けて破滅ＥＮＤを回避する`
9. `/mnt/j/湯けむり`
10. `/mnt/j/Nameless/DeepOne`
11. `/mnt/j/lilith/LPK-30008`
12. `/mnt/j/Kalmia8/鳥籠のマリアージュ`
13. `/mnt/j/YuzuSoft/のーぶる☆わーくす`
14. `/mnt/j/車輪の国、向日葵の少女`
15. `/mnt/j/miel/taimakenshi`
16. `/mnt/j/miel/傲慢巨乳魔王ルシファー、追放された底辺召喚士の絶対服従孕ませ使い魔に堕ちる`
17. `/mnt/j/Pinpoint/王女＆女騎士Ｗド下品露出`
18. `/mnt/j/アパタイト/ダメダメなボクに舞い降りた全肯定ママ女神！～すごいね、いっぱい頑張ったんだね♪♪～`
19. `/mnt/j/スタジオポーク/Moto Yankee Tsuma Hinako ~Shinshin Tomoni Kanzen Netori!~`

`KAI/walpurgis` was removed because the user could not make it run on Windows.
`車輪の国、向日葵の少女` replaced it.

`/mnt/j/NEKO WORKs/nekopara_vol0` is explicitly outside the gate. The user
agreed that its title-specific engine/plugin requirements are better suited to
a dedicated port than to arbitrary-game interpreter scope.

## 6. Honest status of the gate

Do not copy the current “17 compatible” headline without qualification. The
manifest and docs are internally inconsistent with hardware evidence.

Known results from the conversation:

| Title | Host manifest | Physical/user evidence | Honest current status |
| --- | --- | --- | --- |
| 色情教団 | `phase1` | User reported it works after Phase-1 filter and `System.checkAppId` compatibility work | Hardware-confirmed working at that checkpoint |
| swap_re | `phase1` | User accepted the optimized version as playable on Vita | Hardware-confirmed playable at that checkpoint |
| 通淫母 | `phase1` | User reported it works; earlier apparent black screen was extremely slow verbose startup | Hardware-confirmed working at that checkpoint |
| 湯けむり | `phase1` | After read/write/log/inline-script fixes, user reported “The game now works fine” | Hardware-confirmed working at that checkpoint |
| こんそめ！ | `runtime_blocked` | Static invalid decoded `system/rule20.png` payload | Blocked |
| DeepOne | `phase2` | Filename/filter constraints still require executable-derived analysis | Blocked |
| のーぶる☆わーくす | `phase1` | Latest VPK: black screen, log/inform window opens | **Blocked; manifest is wrong as a product claim** |
| 車輪の国、向日葵の少女 | `phase1` | Boots and enters scenario, but transparent title/message/UI layers render solid black in software rendering; attempted alpha fix did not help | **Blocked; manifest is wrong as a product claim** |
| Remaining rows | mostly `phase1` | No complete per-title physical smoke result is preserved in this handoff | Host-audited only unless fresh hardware evidence exists |

The current manifest file is
`tests/retail_compatibility_manifest.txt`. It still records Noble Works and
Sharin as `phase1`. `docs/RETAIL-COMPATIBILITY-GATE.md` calls Noble
`compatible*` and Sharin runtime-blocked. This split proves that prose and the
executable gate can disagree.

## 7. Validation-infrastructure flaw and required redesign

This redesign is a required deliverable, not optional cleanup after the game
fixes. The current testing is insufficiently thorough and will continue to
produce surprises in supposedly compatible games until the real runtime path
is represented. Adding more literal plugin names or more file-signature checks
without improving behavioral coverage will not solve the false-positive
problem.

The current static gate is valuable but its state names are overloaded:

- `phase1` means the filter/archive/script/media/dependency audit passes;
- users and docs have sometimes read it as “the game works on Vita”;
- plugin capability is a boolean even when the implementation is a no-op or
  control-flow-only fallback;
- a physical visual failure has no first-class machine-readable state;
- the final product gate cannot actually prove a physical smoke run occurred.

The next agent should separate these concepts rather than add another special
case.

Recommended model:

1. Keep the current immutable corpus/static audit, but rename its positive
   meaning in output and docs to something like `host_audit_passed`.
2. Give plugins and engine features explicit fidelity levels:
   - `native_exact` or `portable_equivalent`;
   - `behavioral_subset`;
   - `control_flow_fallback`;
   - `link_only`;
   - `unsupported`.
3. Record which exact methods/features each game reaches. A boolean
   `yuri_plugin_link_is_supported()` is too coarse. For example,
   `layerExDraw.dll` can be name-linkable while `drawImageAffine` pixels remain
   absent.
4. Add a separate physical-Vita evidence manifest. A useful receipt should
   include:
   - game id;
   - VPK SHA-256;
   - test date;
   - Vita firmware/device identifier if the user wants it recorded;
   - boot/title/new-game/text/input/audio/save/load status;
   - rendering status;
   - first blocking diagnostic or visible symptom;
   - log/trace artifact path;
   - whether movies or optional modes were exercised.
5. The product compatibility claim should fail closed unless a current
   physical receipt says the required smoke points passed for the exact VPK
   hash. Host tests cannot honestly prove the physical renderer, Vita storage
   timing, plugin registry, or modal UI behavior.
6. A host test can and should verify that docs, the static manifest, and the
   physical-evidence manifest agree. It should reject `compatible` wording for
   a title with a blocked or absent hardware receipt.
7. Expand Noble's regression from “all PSBs parse” to the real TJS/native
   surfaces used by `scenestart`: SQLite row -> octet -> PSBFile construction ->
   exact expected property access -> scene state. Use Yuri's actual TJS VM.
8. Add a sealed-plugin registry runtime test using the same static archive and
   loader path as the Vita ELF. Merely compiling a source file or finding a
   string in the ELF is insufficient.
9. Treat no-op rendering methods as blockers when reachable, even if startup
   no longer throws.
10. Add end-to-end per-title startup/scenario probes where feasible. These
    should run the actual Yuri TJS VM, real storage/archive adapters, native
    plugin loader, and game scripts far enough to observe meaningful layer and
    control-flow state. A scanner that merely finds names cannot replace this.
11. Add negative assertions for placeholders: if a reachable game path calls a
    method implemented as a no-op or returns fabricated default data, the gate
    must report that exact semantic gap instead of counting the plugin as
    supported.
12. Keep the gate layered and explicit in its output: static/data audit,
    behavioral host execution, ARMv7 ABI probes, and physical Vita smoke are
    separate evidence levels. Never collapse them into one `compatible` bit.

Do not promise that host-side tests alone can prove a game **will** work on
Vita. They can make false positives much harder and can prove deterministic
engine/data contracts, but the final rendering/platform gate must remain a
physical-Vita run.

## 8. High-priority Noble Works investigation plan

The next work should start with evidence from the actual game and actual Yuri
plugin loader, not another generic rendering guess.

### Priority A: explain the log/inform window

1. Obtain or inspect the exact latest `krkrvita.log` and boot trace from the VPK
   with SHA `a75c3942...` if available.
2. Determine whether `custom.tjs(57)` still throws
   `Cannot load Plugin layerExDraw.dll`.
3. Verify whether these hardware markers appear before the failure:
   - `retail-motionplayer-surface-ready`
   - `retail-layerexdraw-surface-ready`
   - `retail-scriptsex-surface-ready`
   - `retail-psbfile-ready`
4. Trace `TVPLoadInternalPlugins()`/ncbind registry population and
   `Plugins.link()` lookup. Check exact lowercase/case-normalized module names,
   archive retention, static constructors/callback registration, and whether
   the source is in the final plugin archive rather than only compile commands.
5. Build a host-side sealed-loader test that calls the real `Plugins.link` for
   these names and fails unless it succeeds. Do not substitute direct execution
   of the TJS surface literal.

### Priority B: establish which no-op surfaces are reached

Inspect the actual Noble Works scripts and plugin DLL inventory with bounded
tools. Cross-reference calls to:

- `Motion.Player.draw`, resource lookup, and layer adaptors;
- `Layer.drawImage*` and `GdiPlus.Image` methods;
- `Scripts.*` helpers beyond `getObjectCount`;
- `layerExRaster`, `layerExBtoA`, `layerExSubImage`, and `windowEx` globals;
- PSD layer construction;
- PSB scene object access.

The current fallback sources contain many explicit no-ops. If a startup/title
path uses one to create the visible frame, a black screen is expected behavior,
not a mysterious compositor bug.

### Priority C: compare authoritative implementations

Before extending any fallback, search narrowly in the named Kirikiroid2/Yuri,
KrKr2-Next, krkr2, and krkrsdl2 reference trees. Reuse the real portable code
if present. Record the exact source and revision. Only write a substitute when
no compatible implementation exists, and document its fidelity.

### Priority D: test the full scene boundary

The parser test proves bytes, not game behavior. Add a Yuri-TJS test that uses
the real SQLite adapter and `PSBFile` native class with an installed Noble Works
scene row. Assert the exact object/property operations performed by
`KAGEnvPlayer.tjs`. If the original plugin returns a root dictionary directly
or proxies members, match that contract.

### Priority E: build and physical retest

Only after the first blocking path is reproduced and covered by a failing test:

```sh
cmake --build build-host --parallel 12
ctest --test-dir build-host --output-on-failure --parallel 1
cmake --build build-vita-yuri --target krkrvita-yuri.vpk-vpk --parallel 12
sha256sum build-vita-yuri/krkrvita-yuri.vpk
```

Report the new hash. Do not call it fixed until the user confirms title,
New Game, readable visible output, and basic progression on physical Vita.

## 9. Relevant current code and what it really provides

### PSB/SQLite

- `include/krkrvita/psb.hpp`
- `src/engine/retail/psb.cpp`
- `src/engine/retail/yuri_psbfile_module.cpp`
- `src/engine/retail/yuri_sqlite_module.cpp`
- `tests/test_psb.cpp`
- `tests/test_yuri_sqlite_module.cpp`

The PSB parser is strict. Do not loosen its offset checks merely to make a
corrupt/truncated input pass. The correct response to the prior offset error
was to establish byte integrity and short-read handling.

### Short-read handling

- `include/krkrvita/read_all.hpp`
- generated `build-vita-yuri/generated/yuri/StorageImpl.cpp`
- generated `build-vita-yuri/generated/yuri/XP3Archive.cpp`
- generator logic in `cmake/YuriBackend.cmake` around lines 164-830
- verifier logic in `cmake/VerifyYuriBuild.cmake` around lines 150-205

### Plugin inventory and fidelity

- `include/krkrvita/yuri_plugin_capabilities.hpp`
- `include/krkrvita/native_plugin_inventory.hpp`
- `src/common/native_plugin_inventory.cpp`
- `tests/test_retail_compatibility.cpp`

The native plugin inventory already walks bounded game directories, finds
`.dll`/`.tpm` files, parses PE headers/imports, and hashes them. DLL presence and
imports help identify dependencies, but do not reveal the complete TJS API or
prove compatibility. Script call-site analysis and an authoritative plugin
implementation/oracle are still required.

### Current partial/fallback modules

- `src/engine/retail/yuri_extnagano_module.cpp`: maps documented closed-source
  transitions to crossfade; not pixel-identical.
- `src/engine/retail/yuri_krflash_module.cpp`: load-only; no Flash playback.
- `src/engine/retail/yuri_gfxeffect_module.cpp`: script surface/no-op pixel
  kernel.
- `src/engine/retail/yuri_motionplayer_module.cpp`: control-flow surface; no
  real E-mote/PSB rendering.
- `src/engine/retail/yuri_layerexdraw_module.cpp`: script surface; many image
  operations are no-ops.
- `src/engine/retail/yuri_scriptsex_module.cpp`: partial introspection surface.

These must not be counted as full plugin support.

### Reused implementations already integrated or pinned

The tree intentionally reuses upstream code where available, including Yuri
core/plugins, pinned Kirikiri `layerExImage`, upstream Squirrel, a maintained
PSD plugin source, and other reviewed portable implementations. See
`CMakeLists.txt` FetchContent declarations and `cmake/YuriBackend.cmake`.
Continue that policy.

## 10. Other important title history

### 湯けむり

Initial failures included missing `shrinkCopy.dll`, corrupt-looking KAG system
variables, minutes-long black screens, and huge inline-script/log/storage I/O
costs. Work included:

- shrinkCopy implementation and memory/overflow hardening;
- save-variable recovery/quarantine;
- positive short-read handling;
- buffered UTF-16 writes;
- KAG verbose-log clamping;
- two-pass large inline-script assembly;
- font/storage performance work.

The user eventually reported: “The game now works fine.” Preserve those fixes.

### 通淫母

The apparent black screen was initially a very slow traversal of a huge macro
file with synchronous verbose log I/O. Plugin aliases/surfaces and KAG logging
policy were added. The user later confirmed it works.

### 色情教団

Phase-1 candidate canonicalization was corrected so the unbounded XOR-hash
filter wins instead of an artificial bounded variant. `System.checkAppId`
compatibility was also added. The user later reported it works.

### swap_re

OpenCV-compatible resize/warp/box-filter and direct software-render paths were
heavily optimized and differentially tested. The user reported the new version
fully saturates one ARM core and accepted it as playable on a 496 MHz
Cortex-A9. Preserve the bounded in-place box-filter workspace; do not restore a
full-frame clone hot path.

### 車輪の国、向日葵の少女

The Windows executable patch bypasses a DVD-volume check. Vita now supplies an
equivalent virtual-CD `Storages.searchCD(nonEmptyLabel)` result. This lets the
game run, but the physical Vita software renderer still turns transparency in
the title UI/message box/other graphics into solid black. An ARMv7
destination-additive-alpha rounding correction was real but did not fix the
hardware image. The user asked that this failed attempt be documented honestly
and moved on for that session. It remains blocked.

## 11. Build and test commands

Primary full pipeline:

```sh
cd /home/shoui/krkrvita
JOBS=12 ./scripts/build-vita.sh
```

That script configures/builds/tests Release host, configures/builds/tests
ASan+UBSan host, then builds the Vita VPK. It can take significant time.

Targeted host work:

```sh
cmake --build build-host --parallel 12 --target krkrvita-psb-test
./build-host/krkrvita-psb-test
ctest --test-dir build-host --output-on-failure -R \
  'krkrvita-psb|krkrvita-yuri-sqlite-module|krkrvita-retail-compatibility-contracts|krkrvita-tests'
```

Vita package only:

```sh
cmake --build build-vita-yuri \
  --target krkrvita-yuri.vpk-vpk --parallel 12
```

Explicit generated-source verification:

```sh
cmake \
  -DCOMPILE_COMMANDS=/home/shoui/krkrvita/build-vita-yuri/compile_commands.json \
  -DGENERATED_DIR=/home/shoui/krkrvita/build-vita-yuri/generated/yuri \
  -DSOURCE_DIR=/home/shoui/krkrvita \
  -P /home/shoui/krkrvita/cmake/VerifyYuriBuild.cmake
```

Static retail gate:

```sh
./scripts/run-retail-compatibility.sh
./scripts/run-retail-compatibility.sh --require-all
```

The second command is expected to fail while runtime/Phase-2 blockers remain.
Do not reinterpret a non-`--require-all` pass as product compatibility.

Safe Cortex-A9 binary run:

```sh
./scripts/run-cortex-a9-board.sh /absolute/path/to/one/static-armv7-probe
```

Do not use `scripts/run-retail-matrix-on-cortex-a9.sh` in its current form: it
calls the prohibited old compatibility wrapper.

## 12. Worktree condition

At handoff time, tracked modifications include major changes to:

- `CMakeLists.txt`
- `cmake/` generated-backend/verifier logic
- `docs/`
- filter/profile/game/XP3 sources
- Vita launch/input/presentation sources
- Yuri platform integration
- `tests/test_main.cpp`

There are many untracked compatibility headers, plugin modules, tests,
third-party sources, scripts, and generated support directories. `vendor/krkrsdl2`
also appears dirty as a submodule. Do not assume untracked means disposable.

Use these non-destructive commands to orient:

```sh
git status --short --branch
git diff --stat
git diff --check
```

Do not normalize, reformat, or commit the whole tree while fixing one issue.

## 13. Documentation corrections still needed

The next agent should update the executable status model first or alongside the
Noble investigation, then correct prose. At minimum:

- Noble Works must be runtime-blocked until a physical Vita smoke run passes.
- Sharin must not remain a positive `phase1` product claim while its physical
  renderer is broken.
- `docs/RETAIL-COMPATIBILITY-GATE.md` currently says “17 compatible, 1
  runtime-blocked and 1 Phase-2 blocked,” but its own table contains two
  runtime-blocked titles (Consome and Sharin) plus Noble `compatible*`. The
  headline is inconsistent.
- `tests/retail_compatibility_manifest.txt` disagrees with the prose for
  Sharin and with hardware for Noble.
- `docs/YURI-COMPATIBILITY.md` correctly warns that host checks cannot prove
  rendering, but the status machinery still permits misleading positive
  summaries.

Do not paper over this by changing only prose. The gate should mechanically
reject stale or contradictory hardware claims.

## 14. Definition of done for Noble Works

Noble Works is not done when it merely:

- decrypts all archives;
- compiles scripts;
- links a class name;
- parses all PSBs;
- reaches the title once;
- stops throwing one specific exception;
- passes host CTest;
- builds a VPK.

For this compatibility gate, the minimum honest completion is a physical-Vita
test of the exact VPK hash showing:

1. no modal “Cannot load Plugin”/log window blocking startup;
2. a visible, usable title screen;
3. New Game succeeds;
4. the first scenario renders visible graphics and readable text;
5. input advances the scenario;
6. no immediate PSB/SQLite/Motion/layer exception occurs;
7. basic audio behavior is acceptable;
8. save/load is smoke-tested if reachable;
9. any non-pixel-identical optional effect is explicitly documented.

Until then, report: **Noble Works host audit passes, physical Vita runtime is
blocked by black screen/plugin-log behavior.**

## 15. Final warning against the previous failure mode

The recurring project mistake has been to fix the first exception, add a
callable placeholder, see host tests pass, and promote the title before the
real game path is exercised. Noble Works demonstrates why that is unsafe:

```text
missing Motion -> callable no-op Motion -> layerExDraw link/inform issue
-> title reached -> PSB scene error -> short-read/parser tests pass
-> latest physical VPK still black
```

Advance one blocker at a time, but keep the title blocked until the complete
physical smoke criteria pass. A fallback can be useful engineering without
being compatibility.
