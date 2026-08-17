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

## 16. Session update — 2026-08-17

This section supersedes earlier statements where they conflict. Everything
below was verified in this session; the one open item is called out as such.

### 16.1 Section 3.2's contradiction is resolved: the sealed registry is fine

The registry accepts `layerExDraw.dll`, `scriptsEx.dll` and
`motionplayer.dll`. This is no longer inferred from a marker string in the
ELF. `tests/test_yuri_plugin_registry.cpp` links the real module translation
units plus `ncbind.cpp`, calls `ncbAutoRegister::AllRegist()` and then the same
`TVPRegisteredPlugins` / `LoadModule` sequence the generated Vita
`PluginImpl.cpp` uses, with a `TVPExecuteScript` that really compiles and runs
each fallback on Yuri's TJS VM. It also checks re-link and case-folded link,
one-shot registration, the boot traces, and that an unregistered name still
fails.

Supporting facts:

- `ncbAutoRegister::LoadModule` lowercases, and `AllRegist` lowercases the map
  key, so `NCB_MODULE_NAME TJS_W("layerExDraw.dll")` matches.
- `Application.cpp` calls `TVPLoadPluigins()` (which calls
  `TVPLoadInternalPlugins()` -> `AllRegist()`) before
  `TVPInitializeStartupScript()`, so ordering is not the problem.

The `Cannot load Plugin layerExDraw.dll` log quoted in section 3.2 predates the
`yuri_layerexdraw_module.cpp` work. Do not keep chasing it.

### 16.2 Correction: Yuri does have the Scripts class

An earlier reading of `vendor/yuri/src/core/base/ScriptMgnIntf.cpp` suggested
`tTJSNC_Scripts` was declared but never implemented or registered. That was a
tooling artifact: Yuri's core sources are Shift-JIS, so plain `grep` treats
them as binary and silently reports nothing. **Always pass `-a` when grepping
`vendor/yuri/src/core` or decoded retail scripts.** `Scripts` is implemented
and registered normally.

### 16.3 Two real defects found and fixed

Both were reachable from Noble Works' scripts and both were invisible to the
old checks.

1. `GdiPlus.Image.Clone()` threw `Not a function or invalid method/property
   type` on every call. Inside a TJS2 method, the unqualified class name
   resolves to the constructor member on `this`, not to the class object, so
   `new KrkrVitaGdiPlusImage()` had to become `new global.KrkrVitaGdiPlusImage()`.
2. `Scripts.getObjectCount()` always returned `0`. The TJS fallback read
   `value.count`, and TJS2 dictionaries have no `count` member, so the one
   startup helper the fallback existed for never worked.

### 16.4 scriptsEx is now the upstream implementation

`third_party/scriptsEx/scriptsEx.cpp` is the portable wamsoft implementation
taken from KrKr2-Next (`1abd1ed4`, provenance in the directory README, one
include-path change). It attaches to Kirikiri's built-in `Scripts` class as
upstream does. Verified against the real TJS VM: `getObjectCount`,
sorted `getObjectKeys`, deep `clone`, structural `equalStruct`,
`propGet`/`propSet`, `getObjectContext`, and MD5. The hand-written surface
header and its verifier contracts are gone.

### 16.5 Plugin fidelity levels exist now

`krkrvita::YuriPluginFidelity` in `include/krkrvita/yuri_plugin_capabilities.hpp`
replaces the single "supported" bit with `unsupported`, `link_only`,
`control_flow_fallback`, `behavioral_subset`, `portable_equivalent`, plus
`yuri_plugin_fidelity_is_placeholder()`. `motionplayer.dll`, `layerExDraw.dll`
and `gfxeffect.dll` are `control_flow_fallback`: every name exists and returns,
and everything that would produce pixels is a no-op. Pinned in
`tests/test_retail_compatibility.cpp`, and `docs/YURI-COMPATIBILITY.md` uses the
same words.

### 16.6 The gate has a physical-evidence layer

- `tests/retail_hardware_evidence.txt` — receipts keyed by game id and VPK
  SHA-256, status `passed` or `blocked`, with a symptom.
- New manifest state `hardware_blocked`: the host audit must still pass, and a
  matching `blocked` receipt must exist. `nobleworks` and `sharin` are now
  `hardware_blocked` instead of `phase1`.
- `cmake/VerifyCompatibilityClaims.cmake` (ctest
  `krkrvita-compatibility-claims`) rejects any disagreement between the docs
  table, the manifest and the receipts. A blocked receipt outranks every host
  result. Mutation-tested: flipping `nobleworks` back to `phase1` fails it.
- `scripts/run-retail-compatibility.sh` now reports "host-audit passed" rather
  than "compatible", counts hardware blockers separately, and `--require-all`
  fails while any exist.
- `docs/RETAIL-COMPATIBILITY-GATE.md` headline and table were rewritten; the
  `compatible*` row is gone. The doc also now warns that
  `scripts/run-retail-matrix-on-cortex-a9.sh` still calls the prohibited
  `run-cortex-a9-compatibility.sh` and must not be run as it stands.

### 16.7 Why Noble Works is black — the supported explanation

`custom.tjs` was extracted and read directly (main.xp3, identity filter). It
contains exactly two `System.inform` sites: the pre-rendered font registration
block, and `custom.tjs(57)`'s caught `Plugins.link("layerExDraw.dll")`.

More importantly, `system/Initialize.tjs` (data.xp3) installs a release
`System.exceptionHandler` that calls `Debug.logAsError()`, then
`System.inform(e.message)`, then **`System.terminate()`**. So *any* uncaught
startup exception produces exactly the reported symptom: a black frame with a
message window, and a title that never becomes usable. The window is not
necessarily a plug-in message.

Independently of which exception fires, the current fallbacks cannot render
this title: Noble Works is KAGEX and builds its title screen and message
window through `layerExDraw`, whose image and affine operations are no-ops. A
black frame is the expected consequence, not an unexplained compositor bug.

Noble Works also ships `nobleworks.tpm`, `yuzuex.dll` and `kagexopt.dll`,
none of which the sealed registry implements.

### 16.8 Authoritative implementations exist — use them

`~/KrKr2-Next/cpp/plugins/` contains real portable ports of several plug-ins
this project currently fakes: `layerex_draw/`, `motionplayer/`, `psbfile/`,
`layerExRaster.cpp`, `layerExBTOA.cpp`, `windowEx.cpp`, `psdfile/`,
`scriptsEx.cpp` (now adopted). Caveats:

- `layerex_draw/general/` needs **libgdiplus** (glib/cairo/pango) on non-Windows.
  Adopting it on Vita is a port in its own right, not a drop-in. It is
  nevertheless the highest-value item for Noble Works.
- `motionplayer/` has real Player/ResourceManager/layer machinery over their
  `psbfile`, but its `EmotePlayer` is still a stub upstream.

### 16.9 Open item: the hardware log

The one thing that cannot be derived on the host is which exception the release
`System.exceptionHandler` is reporting. Ask the user for the inform window's
text, or for `ux0:data/krkrvita/engine.log` and the KAG log from a run of the
exact VPK. Everything else in the Priority A plan is now answered.

### 16.10 Build state at the end of this session

- Host: `41/41` ctest before the gate work, all green after with the two new
  tests added.
- Vita: `krkrvita-yuri.vpk` rebuilt with the vendored scriptsEx; generated
  source, ELF and VPK contract verifiers all passed.
- New VPK SHA-256: `d83f82ddf55a70a8c53ea817532d5477455722df6d30ebc31a183a250490217e`
- This VPK has **no** hardware receipt. It fixes two real script-surface
  defects; it is not expected to fix the Noble Works black screen, because
  layerExDraw still draws nothing.

## 17. Noble Works root cause found — 2026-08-17 (later)

The user supplied the device log. It invalidates the guesswork in §16.7.

### 17.1 What actually happens

Noble Works reaches the title screen and New Game normally. It dies at:

```text
start.ks(9)  [scenestart]
kagenvplayer.tjs(1323)[restore]  env.onRestore((new PSBFile(obj)).root);
trace: kagenvplayer.tjs(645)[goToPoint] <-- kagenvplayer.tjs(579)[startScene]
Cannot parse PSB: PSB table offset is out of range
```

The "black screen with a log window" is the fatal-error dialog sitting over an
unpainted frame. It is **not** a rendering problem and not a plug-in link
problem. §16.7's reasoning about layerExDraw causing the black frame was wrong.

### 17.2 Root cause: an off-by-one in the PSB header precondition

`src/engine/retail/psb.cpp` required every header offset to satisfy
`offset < size`. That is correct for the five *tables* (names, strings,
chunkOffsets, chunkLengths, entries), which each start with a type byte. It is
wrong for the two payload *bases*, `offsetStringsData` and `offsetChunkData`:
those are indexing bases, and an empty payload region legitimately begins one
past the last byte.

Every Noble Works scene state embeds no resources, so `offsetChunkData == size`
exactly, and the parser rejected all of them. Fixed by splitting the check:
tables keep `< size`, payload bases use `<= size`. Individual reads were
already bounds-checked at use time, so nothing was loosened.

### 17.3 Why the host gate missed it

`tests/test_psb.cpp` only read the `data` column of `scene.sdb` (437 rows).
The blobs the game feeds to `PSBFile` are `text.state` in **`scenedata.sdb`**
(57,691 rows) — never tested. Measured against the real data:

- before the fix: 0 parsed, 57,691 failed, every one with the exact hardware
  message;
- after: 57,691 parsed, 0 failed.

`test_psb.cpp` now parses every `scenedata.sdb` state blob and pins both sides
of the boundary (a table offset at the end is still rejected; a payload base at
the end is accepted; one past the end is rejected). Mutation-tested: restoring
the old comparison fails the test.

This is the sharpest example so far of the §7 problem. The audit was not
merely shallow — it tested a *different table in a different file* from the one
the game uses and reported that as PSB coverage.

### 17.4 Remaining known issues in the same log (all non-fatal)

- `layerExRaster.dll` (`affinelayer.tjs(4)`), `layerExBtoA.dll`
  (`gfx_movie.tjs(4)`) and `layerExSubImage.dll` (`psdlayer.tjs(4)`) are not in
  the sealed registry. Each call **is** inside `try { } catch(e) {}` behind a
  `typeof global.Layer.<method> == "undefined"` guard; the log lines are
  Kirikiri's debug-mode logging of caught exceptions, not aborts. The script
  units run to completion. The cost is the missing feature only: raster scroll
  (`Layer.copyRaster`), movie bottom-blue-to-top-alpha, and subimage. Portable
  implementations exist in `~/KrKr2-Next/cpp/plugins/` (layerExRaster.cpp is
  77 lines).
- `windowEx.dll` (`mainwindow.tjs(23)`) is caught — harmless.
- `Storages.getLastModifiedFileTime` does not exist anywhere in Yuri or this
  tree; `kagenvironment.tjs(619)` calls it per transition and throws each time.
- `custom.tjs(458)` `kag.searchScenarioMenuItem` is caught — harmless.
- `nobleworks.tpm` is skipped as a Windows-only plug-in, as expected.

### 17.5 Build

- Host: full ctest green, `krkrvita-psb-test` covers the real corpus.
- VPK SHA-256: `615de84a8f839d6cd7cdd98f25fcec4cd1fbff9c1af71903cdcf3819cb98b422`
- Noble Works stays `hardware_blocked` until a physical run of that exact hash
  passes the §14 smoke points. The scenestart blocker is fixed and verified
  against the real 57,691-blob corpus, but that is a host result.

## 18. Vita memory budget — why OOM happened with 365 MiB free

Reported after the PSB fix: Noble Works now runs the scenario, then dies with

```text
Cannot allocate memory for Bitmap : at TVPAllocBitmapBits (size=3686440(1280x720))
```

preceded by `yuri-bitmap-memory-pressure` and four `yuri-bitmap-oom-recovered`.
The user notes the build has ATTRIBUTE2=12 (~365 MiB USER_RW, 112 MiB CDRAM),
and that other games hit OOM too. Two independent causes, both fixed.

### 18.1 VitaGL was hoarding USER_RW

`vglInitExtended`'s fourth parameter is **the RAM left to the application**,
not the size of VitaGL's pool: VitaGL claims everything above it. The presenter
passed a constant 80 MiB. So on a 365 MiB build:

```
365  USER_RW total
-128  fixed newlib heap (reserved before main)
- ~25  code, data, stacks
=~210  free at VitaGL init
-  80  left to the application by the constant threshold
=~130  MiB taken by VitaGL
```

VitaGL is used here only to blit the software framebuffer — five 960x544 RGBA
presentation textures, about 10 MiB. Roughly 130 MiB sat idle in its pool while
the engine starved.

Now `application_ram_threshold()` in `src/engine/vita/vitagl_presenter.cpp`
calls `sceKernelGetFreeMemorySize()` and passes
`free_user - kVitaGlPoolBytes` (48 MiB), floored at
`kVitaGlMinApplicationRamThresholdBytes` (64 MiB). It emits
`vitagl-user-ram-free-<N>m-threshold-<N>m` so the real figures are in the log
instead of being inferred.

**This is the one assumption still needing hardware confirmation.** If 48 MiB
turns out to be too small for VitaGL's GXM/shader-patcher buffers, init fails
loudly and `kVitaGlPoolBytes` is the single constant to raise.

### 18.2 The large-bitmap tier had a fixed 64 MiB ceiling

`kVitaBitmapMemblockBudget` capped USER_RW memblocks for bitmaps at 64 MiB —
about 18 full-screen 1280x720 surfaces. Past that, every large bitmap fell back
to the fixed 128 MiB newlib heap, which already holds scripts, TJS objects,
SQLite and FreeType, and which fragments badly under 3.5 MiB requests. The
console still had memory; our own constant refused it.

`vita_bitmap_memblock_budget_allows()` now takes **measured free USER_RW** and
allows an allocation while it leaves `kVitaBitmapMemblockReserveBytes` (32 MiB)
free for the subsystems whose allocations are not recoverable. The malloc
fallback stays as a last resort. Expected result on the user's device: the
bitmap tier grows from 64 MiB to roughly 130 MiB.

### 18.3 Where the remaining memory is

112 MiB of CDRAM is still unused by this design. Bitmaps are CPU-written, so
USER_RW is correct for them, but the presentation textures are GPU-read and
could move to CDRAM if USER_RW pressure returns.

### 18.4 State

- `tests/test_main.cpp` pins both policies as pure functions, including the
  degenerate free-memory reports and the reserve boundary.
- `cmake/VerifyYuriBuild.cmake` now requires the measured-threshold and
  free-memory-gated call sites instead of the old constants.
- Host: 22/22 in the fast set; the slow retail corpus is unaffected by these
  changes and was green earlier in the session.
- VPK SHA-256: `99fa148f4758a8a56e17a2177f023fcb32b418fd981f95e21ef3a7e92e143bda`

## 19. Noble Works is hardware verified — 2026-08-17 (final)

The user retested and reported: works fine, no crash, no visual issues so far.
That is the first passing physical receipt in this project.

### 19.1 vglInitExtended semantics confirmed from source

`~/ps2vita/src/vitagl/source/vgl.c:562` sizes VitaGL's pool as

```c
info.size_user > ram_threshold ? info.size_user - ram_threshold : 0
```

so `ram_threshold` is unambiguously **the RAM left to the application**, and
§18.1's change was in the right direction. Note also that `vglInitExtended`
passes `cdram_threshold = 0`, so VitaGL already owns all 112 MiB of CDRAM; its
USER_RW pool only covers what cannot live there, which is why 48 MiB is ample.

Housekeeping: `~/ps2vita/src/vitagl` is *not* the prohibited tree. VitaSX2 is
specifically `/home/shoui/ps2vita/src/vitasx2-ng`. The prohibition applies to
that path only.

### 19.2 Gate now distinguishes hardware-verified from host-audited

`phase1` + a `passed` receipt yields the label **`hardware verified`**;
`VerifyCompatibilityClaims.cmake` rejects a passing receipt on any row that is
not `phase1`, and still rejects a blocked receipt on a positive row.
Mutation-tested both ways. Corpus is now 15 host-audit passed, 1 hardware
verified (nobleworks), 1 hardware-blocked (sharin), 1 runtime-blocked, 1
phase-2 blocked.

### 19.3 VPK hashes

The receipt names `99fa148f4758a8a56e17a2177f023fcb32b418fd981f95e21ef3a7e92e143bda`,
which is the binary the user actually ran. A later rebuild produced
`111c22b907f16f979cc58fbc59cf87d659107b800a854a7da68a8a50c756406b`; the only
delta is a source comment recording the vgl.c citation above, so there is no
behavioural difference — but the receipt deliberately still names the tested
hash rather than the newest build.

### 19.4 Not yet exercised on hardware

Save/load, long-session stability, and the movie path. Still outstanding in the
engine regardless of this title:

- `layerExRaster.dll`, `layerExBtoA.dll`, `layerExSubImage.dll` are not in the
  sealed registry. Their `Plugins.link` calls in `affinelayer.tjs`,
  `gfx_movie.tjs` and `psdlayer.tjs` are all inside `try`/`catch` behind
  feature guards, so nothing is skipped and only the optional effects are
  absent. Portable implementations exist in `~/KrKr2-Next/cpp/plugins/`.
- `Storages.getLastModifiedFileTime` does not exist; `kagenvironment.tjs(619)`
  throws on it once per transition.
- `motionplayer` and `layerExDraw` remain `control_flow_fallback`. Noble Works
  shows no visual defect from this, but a title that leans harder on them will.

## 20. allokmama cf-file refusal — 2026-08-17

`ダメダメなボクに舞い降りた全肯定ママ女神` exited at boot with
`cfファイルがありません。終了します。`.

### 20.1 Cause

`script/first.ks` opens with

```tjs
var file = ( Storages.chopStorageExt( System.exeName ) + ".cf" );
if( Storages.isExistentStorage( file ) != true ) {
    System.inform( 'cfファイルがありません。終了します。' );
    System.exit();
}
```

`allokmama.cf` is a loose krkrconf config file in the game folder. On Windows
`System.exeName` is the executable's path, so the expression resolves to it.

Yuri's `ExePath()` returns `TVPNativeProjectDir` — the project *directory*,
because Android has no game executable — so `System.exeName` was
`.../games/<name>/`, `chopStorageExt` found no extension, and the test resolved
to `.../games/<name>/.cf`, which never exists.

### 20.2 Fix

`ExePath()` on Vita now resolves the staged Windows executable via
`krkrvita_yuri_project_executable_path()`, falling back to the directory when
there is no unambiguous answer. The selection rule
(`include/krkrvita/vita_executable_name.hpp`) is a pure function:

1. candidates are directory entries ending in `.exe`, case-insensitively;
2. prefer one with a sibling `<base>.cf` — Kirikiri's own krkrconf pairing,
   which distinguishes the engine from shipped tools;
3. otherwise take the single candidate;
4. otherwise return nothing and keep Yuri's directory behaviour.

Step 4 matters: guessing would change `System.exeName` for titles that already
boot.

### 20.3 Blast radius, measured

Live `ExePath()` consumers on Vita are only `System.exeName` and a Debug dump
filename. `TVPGetAppPath()` (`System.exePath`) uses `TVPProjectDir` — its
`ExePath()` branch is `#if 0` — and the Windows project-directory search that
calls `TVPIsXP3Archive(ExePath())` is inside `#if 0` too. So nothing else
changes behaviour.

Applying the rule to the whole corpus: it now names an executable for
pl0002, kayoinbo, consome, yukemuri, deepone, lpk30008, torikago, taimakenshi,
lucifer, allokmama and hinako, and leaves seishoku, chichimiko, swap_re,
jashin, akuyaku, nobleworks, sharin and oujo_wkishi on the old directory value
(two or more executables, no `.cf` pairing). Noble Works is unaffected.

Of the changed titles, kayoinbo and yukemuri are user-confirmed working, so
they are the ones to watch on retest. The change moves `System.exeName` toward
its Windows meaning, so it should be an improvement, but it is untested on
those two.

### 20.4 Note on shipped plug-ins

This title's `plugin/` folder contains ~48 DLLs including `wsh.dll`,
`win32ole.dll`, `qrcode.dll` and `htmlhelp.dll`. As the user pointed out, that
is a wholesale copy of a plug-in collection, not evidence of use. Do not infer
required capabilities from a game's `plugin/` directory; only a script call
site is evidence.

### 20.5 State

- `tests/test_main.cpp` pins the selection rule against this game's real
  directory layout plus the ambiguous and unrelated-`.cf` cases.
- `cmake/VerifyYuriBuild.cmake` requires the generated `ExePath()` patch and
  the tested selection call.
- Host: 22/22 fast set; allokmama's host audit row still passes.
- VPK SHA-256: `467924ef00f1b180cd0ee153ce4083280237ed62d631838cc6cf0a5baaf26a5a`
- Unverified on hardware. If it still refuses, check that `allokmama.cf` was
  actually copied to the Vita game folder — the fix only makes the path
  resolve; the file still has to be there.

## 21. allokmama crash after the cf fix — sigcheck cross-thread TJS access

With §20 in place the game boots past the cf gate, loads every script, and
reaches `release.ks`. It then coredumps immediately after
`retail-sigcheck-rsa-pss-ready`.

### 21.1 What the coredump does and does not show

`psp2dmp` is gzip; `gunzip` yields a normal ARM ELF core. The module is
non-PIE at 0x81000000, so ELF addresses are runtime addresses and
`arm-vita-eabi-addr2line` works directly.

Faulting thread (`THREAD_REG_INFO` entries are 376 bytes at offset 8: size,
tid, r0-r12, sp, lr, pc, cpsr; `THREAD_INFO` entries are 200 bytes at offset 8:
size, tid, name):

```
tid=0x400402d7 name=pthread  pc=0x81be4de8  lr=0x81131e1f  sp=0x81fbeba0  r0=0
```

`pc` is in `.rodata` (`.text` ends at 0x81bbc728), i.e. an indirect branch
through a corrupted pointer.

**Do not trust the `lr`.** It resolves inside
`tTJSNI_BaseSoundBuffer::Fade`, but disassembly shows `0x81131e1e` is a `beq`,
not a call site, so it is a stale value. The dump contains only stack pages —
no `.text`/`.data` — and the frame is shallow, so there is no recoverable
backtrace. The reliable facts are: an indirect call through a bad pointer, on a
non-main worker thread, immediately after sigcheck registered.

### 21.2 Cause

`release.ks` calls `checkSignature()` per file, which our module ran on a
detached `std::thread`. That worker then did all of this off the main thread:

- `owner->AddRef()` / `owner->Release()` — `iTJSDispatch2` refcounts are plain
  ints, not atomics;
- `TVPPostEvent(...)` — `TVPEventQueue` is a bare `std::vector<tTVPEvent*>`
  with no locking anywhere in `EventIntf.cpp`, drained concurrently by the main
  thread;
- `tTVPEvent`'s constructor AddRefs source and target.

Any of those corrupts an object; the symptom is a later indirect call through
the damaged vtable. This is a general defect affecting every title that uses
sigcheck, not something specific to this game.

### 21.3 Fix

Verification (file I/O + SHA-256/RSA) stays on the worker. The completion is
handed back through `Application->PostUserMessage()`, which is guarded by
`m_msgQueueLock` and drained in `tTVPApplication::ProcessMessages()`. The event
post, the `Release`, and the job-table erase now all run on the main thread.
`cmake/VerifyYuriBuild.cmake` requires the marshalling and forbids the old
worker-side sequence.

### 21.4 Expected next behaviour — not yet a boot

`release.ks` builds its check list as

```tjs
var fn = [ System.exeName.substr(System.exePath.length), 'data.xp3', 'patch.xp3' ];
```

and `onCheckSignatureDone` does `System.exit()` when `result < 1`.

Before §20, `fn[0]` was the empty string and was skipped. Now it is
`allokmama.exe`, which exists — so it gets verified. **The game folder has
`.sig` files for every archive but none for `allokmama.exe`**, so that check
must fail and the title will stop at `認証エラー`.

That is the game's own tamper gate refusing an executable whose signature is
absent, not an engine defect, and it is not something the port can honestly
"fix" — there is no signature to verify against. Decide with the user before
doing anything here; suppressing a signature check is a product decision, not a
compatibility fix.

### 21.5 State

- Host: 22/22 fast set.
- VPK SHA-256: `4c345a831b29a8f038ac3686a02e9264b9666fbc863fd4a1a774729aae0c7d39`
- The threading fix is real and worth keeping regardless of how the signature
  question is resolved.

## 22. allokmama crash, second pass — sigcheck did storage I/O off-thread

The §21 fix was incomplete and the game still coredumped at the same point.

### 22.1 Correction to the §21 dump analysis

§21 read the first dump's `lr` as `tTJSNI_BaseSoundBuffer::Fade` and built a
sound-fade story on it. That was wrong, and the second dump shows why: the
thread that my "pc is in app address space" heuristic flags as faulting is
`tid=0x400402d7`, which is the **touch reader** (`pc` = the `sceTouchRead`
import stub in `.vitalink.fs`, `lr` = the `start_touch_reader` lambda in
`src/platform/vita/yuri_input.cpp`). It is simply blocked in a normal syscall.
Every other thread sits in kernel space, so the heuristic picks it every time.

**These dumps do not identify a faulting thread.** They carry only stack pages
(no `.text`/`.data`), no fault-reason note, and nothing usable in `TTY_INFO`.
Do not build a theory on `lr` alone; verify against `nm` and a disassembly that
the address is actually a call site, as §21.1 shows.

### 22.2 Actual cause

`verify_storage()` calls `TVPCreateStream()` twice and loops on
`stream->Read()`. §21 moved only the *completion* to the main thread and left
all of that on the detached worker. Yuri's storage layer is main-thread state:
`TVPCreateStream` walks the media manager, the XP3 archive table and the
auto-path table — and the log shows the main thread rebuilding the auto-path
table and loading scenarios throughout exactly this window.

So the worker and the main thread were mutating the same storage structures
concurrently. That is the crash, and `_malloc_r` on the stack in the first dump
fits heap contention.

### 22.3 Fix

The worker is gone. `checkSignature` verifies inline on the calling thread and
delivers the result with `TVPPostEvent`, so the script's asynchronous contract
still holds: it gets a handler immediately and `onCheckSignatureDone` arrives
through normal event dispatch. `cancelCheckSignature` now honestly reports
"nothing cancelled", because verification finishes before the call returns.

Trade-off: a startup pause while `data.xp3` (11 MB) and the executable (3.8 MB)
are hashed. The title blocks on the result anyway. If that pause ever becomes
a problem, the fix is chunked hashing driven from the main loop — **not** a
worker thread.

`cmake/VerifyYuriBuild.cmake` now forbids `std::thread` in this file outright.

### 22.4 Still expected: 認証エラー

§21.4 stands. `allokmama.exe` has no `.sig` in the game folder, so once the
crash is out of the way the signature check should fail and the title should
exit with `認証エラー`. That is the game's own tamper gate, and it is the
user's call what to do about it.

### 22.5 State

- Host: 22/22 fast set.
- VPK SHA-256: `9c0a289920bc3f9ef35f5dd0d8dc70c786fada4700bc9297da5fae41511461b1`

## 23. Use the user's crash parser — and the corrected allokmama root cause

### 23.1 Tooling: /home/shoui/ps2vita/vita-ultra-parse

There is a working Vita coredump analyser at
`/home/shoui/ps2vita/vita-ultra-parse`. **Use it.** Do not hand-roll psp2dmp
parsing as §21 and §22 did; that produced two wrong diagnoses in a row.

```sh
cd /home/shoui/ps2vita/vita-ultra-parse
python3 main.py crash analyze ~/scratch/psp2core-....psp2dmp \
  --elf /home/shoui/krkrvita/build-vita-yuri/krkrvita-yuri \
  --sdk /home/shoui/vitasdk
```

Note `~/ps2vita/vita-ultra-parse` and `~/ps2vita/src/vitagl` are fine to read.
Only `/home/shoui/ps2vita/src/vitasx2-ng` is off limits.

It prints, per thread, the **stop reason** and status, and names the crashed
thread. Stop reasons it knows: `0x30002` undefined instruction, `0x30003`
prefetch abort, `0x30004` data abort, `0x60080` divide by zero. The field lives
at offset `0x74` of each `THREAD_INFO` entry — the hand-written parser in §21
never read it, which is exactly why it guessed.

Its disassembly-around-PC output is wrong for kernel addresses (it disassembles
the app ELF at the raw address), so ignore that section when PC is in
`SceLibKernel`. Threads, stop reasons, registers and the stack dump are sound.

### 23.2 Both allokmama dumps: same thread, same reason

```
psp2core-1786960534 (VPK 467924ef): thread 0x401d02ef CRASHED, stop reason 0x10006
psp2core-1786961907 (VPK 4c345a83): thread 0x401d02ef CRASHED, stop reason 0x10006
```

`0x401d02ef` is the last-created thread in both, its PC is inside SceLibKernel
(so it died in a syscall, not on a CPU fault), and at `release.ks` time the only
thread being created is sigcheck's detached worker — the touch reader
(`0x400402d7`), the vitaGL collector, the movie overlay worker and the startup
self-test threads are all accounted for elsewhere.

So the crashing thread was **our sigcheck `std::thread`**, in both runs.

### 23.3 Retractions

- §21.1's "`lr` resolves to `tTJSNI_BaseSoundBuffer::Fade`" — noise. That was
  the touch reader, the only thread with a PC in app address space.
- §22.1's "these dumps do not identify a faulting thread" — wrong. They do, via
  `stop_reason`. The tooling was the problem, not the dump.

The conclusion §22 reached (storage I/O on a worker thread) is still the right
one, and it is now confirmed rather than inferred: the thread doing that I/O is
the thread the kernel killed.

### 23.4 Fix in place

§22.3's change removes the worker entirely, so thread `0x401d02ef` no longer
exists. VPK `9c0a289920bc3f9ef35f5dd0d8dc70c786fada4700bc9297da5fae41511461b1`.

§21.4/§22.4 still stand: with the crash gone, the missing `allokmama.exe.sig`
should surface as `認証エラー`.

## 24. allokmama: absent signature is not a failed signature

The §22 threading fix worked — no coredump. The run reached `release.ks`,
started both checks, went on into `mode_title.ks`, loaded `title_cfg.ks` and
was already loading title images (`clear`, `medi1`) when the exe result landed:

```
署名確認開始 : 1:file://./ux0:.../allokmama/allokmama.exe
署名確認開始 : 2:file://./ux0:.../allokmama/data.xp3
署名確認結果 : 1:0:can't open signature file
Information: 認証エラー ... Msg : can't open signature file
```

### 24.1 Why §21.4/§22.4 were wrong to call this the game's own gate

The user's decisive evidence: **this game runs fine on Windows through that
same `allokmama.exe`**, and the shipped folder has a `.sig` for every `.xp3`
and none for the executable. `release.ks` exits on `result < 1` unconditionally,
so the original `sigcheck.dll` cannot be reporting a missing signature as a
failure. Our implementation was simply stricter than the plug-in it stands in
for, and refused an intact retail title.

I had twice written this up as "the game's own tamper gate refusing a modified
executable, not an engine defect". That was wrong: it is an engine defect.

### 24.2 Fix

`verify_storage()` now checks `TVPIsExistentStorage(target + ".sig")` first. No
signature file means the target is unsigned, which reports success with no
error. Everything that *does* ship a signature is still verified strictly — a
malformed, unreadable or non-matching `.sig` remains a hard failure, so the
check still covers every file the author actually signed (all the archives).

Security note, stated plainly: this weakens the check to "verify what is
signed" rather than "require everything to be signed". That is the behaviour
the reference plug-in demonstrably has, and matching it is the point of the
port, but it does mean deleting a `.sig` bypasses verification of that file.

Pinned in `cmake/VerifyYuriBuild.cmake`.

### 24.3 State

- Host: 22/22 fast set; `krkrvita-rsa-pss-signature` still green, so strict
  verification of signed payloads is unchanged.
- VPK SHA-256: `9cd2845d8d15a7e6ef881edefd1f052f62e435787d67a3b63ca43529adfd3826`
- Next expected: `data.xp3`'s check (handler 2) actually completes and must
  pass on its real signature. If *that* fails, it is a genuine bug in our
  RSA-PSS/SHA-256 path against real retail data, not a policy question.

## 25. allokmama is hardware verified — 2026-08-17

The user confirms the title now boots and plays on the Vita.

That also closes §24.3's open question: `data.xp3` verified against its real
RSA-PSS/SHA-256 signature, so `src/engine/retail/rsa_pss_signature.cpp` is now
proven against genuine retail signed data, not just the host fixtures.

Four defects had to be fixed to get here, in order:

1. `System.exeName` returned the project directory, so the `.cf` gate could
   never resolve (§20).
2. sigcheck verified on a detached worker thread, doing Yuri storage I/O and
   TJS refcounting off the main thread; the kernel killed that thread
   (§21-§23). Confirmed as thread `0x401d02ef` by the user's crash parser.
3. An absent `.sig` was reported as a verification failure rather than as
   "unsigned" (§24).

Only the first was specific to this title. The threading and signature-policy
defects were general and would have hit any game using sigcheck.

### 25.1 Corpus

14 host-audit passed, **2 hardware verified** (nobleworks, allokmama), 1
hardware-blocked (sharin), 1 runtime-blocked (consome), 1 Phase-2 (deepone).

### 25.2 What this run also validates

allokmama is a second independent confirmation of the two global changes made
earlier today, on a different title and a different engine configuration:

- the PSB payload-base relaxation (§17);
- the memory policy — dynamic VitaGL threshold and free-memory-driven bitmap
  budget (§18). Its log shows the same
  `vitagl-user-ram-free-206m-threshold-158m` measurement.

Neither regressed a title that previously worked.

### 25.3 Receipt

`allokmama|9cd2845d8d15a7e6ef881edefd1f052f62e435787d67a3b63ca43529adfd3826|2026-08-17|passed`

## 26. Regression retest of the previously-working titles — 2026-08-17

The user confirms the other games still work after the `System.exeName` change.
Receipts recorded on VPK
`9cd2845d8d15a7e6ef881edefd1f052f62e435787d67a3b63ca43529adfd3826` for
seishoku, swap_re, kayoinbo and yukemuri.

Two of those had `System.exeName` actually change (§20.3): kayoinbo to
`通淫母.eXe` via the `.cf` pairing rule, yukemuri to `湯けむり.eXe` as the sole
executable. seishoku and swap_re keep the old directory value (two executables,
no `.cf` pairing), so for them this is a check on the day's other global
changes rather than on exeName.

These rows are recorded as `passed` on the strength of a regression retest, not
a fresh full smoke run; the symptom column says so. If a later session needs
save/load or long-session evidence for these titles, it is not yet there.

### 26.1 Corpus

10 host-audit passed, **6 hardware verified** (seishoku, swap_re, kayoinbo,
yukemuri, nobleworks, allokmama), 1 hardware-blocked (sharin), 1
runtime-blocked (consome), 1 Phase-2 (deepone).

### 26.2 What is now de-risked

Every global change made today has been exercised on hardware across six
titles: the PSB payload-base relaxation (§17), the dynamic VitaGL threshold and
free-memory bitmap budget (§18), the vendored scriptsEx (§16.4), the
`System.exeName` resolution (§20), and the sigcheck threading and
absent-signature fixes (§22, §24). No regression reported.

### 26.3 Next

`sharin` is the only remaining hardware-blocked title: transparent
title/message/UI layers composite to solid black under the software renderer.
The earlier ARMv7 destination-alpha rounding fix was real but was not the
cause, so this needs a fresh look at the software compositor rather than
another arithmetic guess.

## 27. Sharin transparency: not fixed, but the blend path is now ruled out

I did not fix this. What I did was close off the largest remaining suspect with
hardware evidence, so the next attempt does not repeat it.

### 27.1 The test that was supposed to cover this never ran, and tested the wrong function

`tests/test_yuri_arm_alpha.cpp` was written for this exact bug — its sample
pixels are commented as coming from Sharin's title UI. Two problems:

- It was referenced by **no** build file. Not CMakeLists, not any script. It
  never ran.
- It compared the `_HDA`, `_a` and `_ao` blend variants. But
  `TVP_BLEND_4` (`LayerBitmapIntf.cpp:1539`) selects the **plain, non-HDA**
  function whenever `hda == false`, which is the case for an `ltAddAlpha`
  layer compositing onto the opaque primary layer. Sharin's actual path was
  never compared against scalar TVPGL.

This is the §7 false-confidence pattern again: a test existed, looked
targeted, and proved nothing.

### 27.2 Hardware result

`scripts/run-arm-alpha-probe.sh` now builds the probe as a static ARMv7 binary
and runs it on the Cortex-A9 board. The test additionally covers plain
`TVPAdditiveAlphaBlend`, `TVPAdditiveAlphaBlend_o`, `TVPAlphaBlend` and
`TVPAlphaBlend_o`, and classifies each mismatch as RGB or alpha-only.

Result on real ARM: **every colour channel matches scalar TVPGL exactly.** All
mismatches are confined to the alpha byte, which is the documented non-HDA
contract — those variants do not hold destination alpha. The probe now fails
only on a colour difference, so it is a meaningful gate rather than noise.

### 27.3 Also eliminated

- **Framebuffer alpha cannot reach the screen.** The main game frame is drawn
  by `present_bound_texture()` with `GL_REPLACE` and `GL_BLEND` disabled. The
  `glEnable(GL_BLEND)` in `vitagl_presenter.cpp` is the movie-overlay path.
- **Layer-type to blend-op mapping is intact**: `ltAddAlpha` → `omAddAlpha` →
  `bmAddAlpha`, and the `LayerBitmapIntf.cpp` dispatch behind it.
- **No missing software blend op**: the software render manager selection is
  just `tTVPSoftwareTexture2D::Create` plus `TVPGetRenderManager("software")`.

### 27.4 The one solid fact about the title

Sharin's `system/MessageLayer.tjs` sets `layerType = ltAddAlpha` with
`frameColor = 0x000000` and `frameOpacity = 128`, and draws the frame with
`fillRect(0, 0, w, h, (frameOpacity << 24) + frameColor)`. A **correct** render
of that message box is a 50%-transparent *black* rectangle.

So "solid black" is precisely what losing the opacity looks like. The blend
functions compute it correctly, so the next attempt should look at what feeds
them: the layer's `opacity`/`absolute` handling, whether `imageModified`/damage
causes the frame fill to be re-issued at full alpha, or the `dfAuto` →
`dfAddAlpha` face selection during `fillRect`.

### 27.5 What would help most

A screenshot. My remaining hypotheses fork on whether the symptom is
"translucent regions render opaque black", "the whole element is a black
rectangle", or "the whole screen is black except opaque art" — and those point
at different code. Two sessions have now been spent inferring the symptom from
a one-line description.

## 28. Sharin: candidate fix — stale IsOpaque erases the layers beneath

The user's screenshot reframed this entirely. The Vita title screen is black
except the menu text and its green link highlights; the Windows original shows
full background art behind the same text. The user confirmed images load fine
in general, and that the first message box is black too.

### 28.1 What the title actually is

`scenario/title2.ks` builds the title menu as

```
@position left=0 top=0 width=800 height=600 frame="" opacity=0 visible=true
```

— a **full-screen ltAddAlpha message layer at opacity 0** holding the buttons,
sitting over `@img storage=t_bg1`. The message box is the same kind of layer at
`frameOpacity = 128`. Both render black; everything drawn *onto* those layers
(text, link highlights) renders correctly.

That shape — "the layer's own content survives, everything beneath it is gone"
— is not a blending fault. It is the compositor skipping the layers underneath.

### 28.2 Cause

`tTJSNI_BaseLayer::QueryUpdateExcludeRect` computes the region that is
completely covered by opaque content, so lower layers can skip drawing. Stock
Kirikiri gates that on `DisplayType == ltOpaque`. Yuri widened it:

```cpp
if (parentvisible && (DisplayType == ltOpaque || (MainImage && MainImage->IsOpaque())) && Opacity == 255)
```

`IsOpaque` is a plain `bool` on `tTVPBitmap` (`win32/LayerBitmapImpl.h:97`,
default false). It is set **true** only by the image loaders
(`GraphicsLoaderIntf.cpp:1064`, `GraphicsLoadThread.cpp:55`) when a decoded
image had no alpha channel, and it is cleared only by the RenderManager texture
wrappers (`SetPoint`, `GetScanLineForWrite`, `CopyFrom`). The ordinary CPU
drawing paths in `LayerBitmapIntf.cpp` — `Fill`, `ColorRect`, `Blt` — never
touch it. A layer that once held an alpha-less image and is later drawn into
with transparency keeps a stale `true`.

### 28.3 Fix

`cmake/YuriBackend.cmake` now patches the generated `LayerIntf.cpp` to trust
the bitmap flag only for layer types that do not composite through an alpha
channel:

```cpp
(DisplayType == ltOpaque ||
 (MainImage && MainImage->IsOpaque() && !TVPIsTypeUsingAlphaChannel(DisplayType)))
```

The error is deliberately one-sided: a false negative costs some redundant
drawing, a false positive erases the frame.

VPK `32d9b37d4fcc21f144cc55281e9e4a62e38de7c6ee25957f807e3bd732d9bc50`.

### 28.4 Status: unproven

This is a candidate, not a confirmed fix. It explains the screenshot exactly
and the change is safe in the failing direction, but no hardware run has
happened. Sharin stays `hardware_blocked` until one does.

### 28.5 Ruled out along the way (do not revisit)

Per §27: the TVPGL blend arithmetic is colour-exact on real ARM for the plain,
`_o`, `_HDA`, `_a` and `_ao` variants; framebuffer alpha never reaches the
screen (`GL_REPLACE`, `GL_BLEND` disabled on the main frame); and the
layer-type to blend-op mapping is intact.

One earlier misreading worth recording: an ARM probe showing
`TVPAdditiveAlphaBlend` losing 1 per channel under a fully transparent source
is real but is **not** this bug — both scalar and NEON share it, and it needs
repeated recomposition of the same pixels to matter.

## 29. Sharin: §28's fix was wrong too — instrumented build instead — 2026-08-17

The user tested the §28 build (VPK `32d9b37d…`). Still black, in three places:
the title screen, the first message box 「まずは自己紹介をしよう」, and
「後ろを振り返らずに歩くこと六時間」.

### 29.1 §28 is disproved, and the patch is confirmed present

Do not re-attempt the `IsOpaque` exclusion theory. The guard is in the built
source — `build-vita-yuri/generated/yuri/LayerIntf.cpp:5602`, and a decoded
diff against vendor shows it is the *only* difference — so the black layers
survive it. The exclusion path is not the mechanism.

Two traps cost time here and will cost it again:

- **`grep` silently skips the generated Yuri sources.** They are Shift-JIS, so
  `grep -n IsOpaque` on `LayerIntf.cpp` prints nothing whether or not the
  patch applied. Always `grep -a`. This is the second time this has produced a
  false conclusion (§7 was the first).
- **`@position opacity=0` is not layer opacity.** `MessageLayer.tjs:488` maps
  the tag's `opacity` to **`frameOpacity`**, so the title layer's `Opacity` is
  255, not 0. §28 briefly looked inapplicable for the opposite reason. The
  fill it produces is `fillRect(0, 0, w, h, (frameOpacity << 24) + frameColor)`
  = `0x00000000` for the title and `0x80000000` for the message box.

### 29.2 What is now eliminated, with evidence

- Blend arithmetic on real ARM (§27.2), all five variants, colour-exact.
- Framebuffer alpha reaching the screen. Re-verified directly, not from notes:
  `present_bound_texture()` uses `GL_REPLACE`, and `configure_2d()`
  (`vitagl_presenter.cpp:131`) calls `glDisable(GL_BLEND)`. Destination alpha
  cannot darken the frame.
- `UpdateDrawFace` maps `ltAddAlpha` → `dfAddAlpha` correctly (`LayerIntf.cpp:1394`).
- `FillRect`'s `dfAddAlpha` branch writes the full ARGB word including alpha
  (`LayerIntf.cpp:3895-3900`), and `FillARGB` writes it unmodified.
- `BltImage`'s type→method mapping is stock (`LayerIntf.cpp:5704-5712`).
- `IsBlendTarget()` is unconditionally true, so `GetTextureForRender` always
  takes the copy-on-write `Independ()` path; the destination is never discarded.
- The message layers' parent is `fore.base`/`back.base`, which
  `GraphicLayer.tjs:336` sets to `ltCoverRect` (= `ltOpaque`). So the composite
  should resolve to `bmAddAlpha`, not the unimplemented `bmAddAlphaOnAlpha`.

### 29.3 Two things found on the way that are real but unproven as causes

- **`bmAddAlphaOnAlpha` is not implemented.** `RenderManager.cpp:2248` has its
  cache entry commented out and `:2356-2359` returns `nullptr` with "Not yet
  implemented"; the CPU path at `LayerBitmapIntf.cpp:1691` is an empty case.
  An `ltAddAlpha` layer over an `ltAlpha` parent therefore silently does not
  composite at all. Sharin's message layers should not hit this, but the probe
  will say whether they do.
- **`REGISER_BLEND_4` registers the plain names using the `_HDA` functions.**
  `RenderManager.cpp:2578-2580` expands `AdditiveAlphaBlend` to
  `tTVPRenderMethod_BltAndOpa<52, TVPAdditiveAlphaBlend_HDA, TVPAdditiveAlphaBlend_HDA_o>`,
  so every `bmAddAlpha` composite runs the hold-destination-alpha variant
  regardless of the `hda` flag. §27.2 measured these as colour-identical, so
  this is a correctness smell rather than a demonstrated defect.

### 29.4 The instrumented build

Rather than ship a third guess, this build measures the three decisions the
remaining hypotheses disagree about. `include/krkrvita/yuri_composite_probe.hpp`
plus `src/platform/vita/yuri_composite_probe.cpp`, injected into the generated
`LayerIntf.cpp` by `cmake/YuriBackend.cmake`:

| probe | site | reports |
| --- | --- | --- |
| `probe-composite` | `BltImage`, before `dest->Blt` | `src` `dst` layer types, resolved `met`, `opa`, `hda`, size |
| `probe-exclude` | `QueryUpdateExcludeRect` | any layer claiming an opaque exclusion rect |
| `probe-fill` | `FillRect`, `dfAlpha`/`dfAddAlpha` branch | `face`, layer type, the ARGB word written |

All three are hard-capped (48/24/24) and the composite and fill probes ignore
surfaces smaller than 320x200. `krkrvita_boot_trace` reopens, writes, syncs and
closes `boot-status.txt` on every call, so an uncapped probe in the compositor
would make the title unbootable rather than diagnosable.

Enum values for reading the output: `ltOpaque=1`, `ltAlpha=2`, `ltAddAlpha=12`;
`bmCopy=0`, `bmAlpha=2`, `bmAddAlpha=11`, `bmAddAlphaOnAddAlpha=12`,
`bmAddAlphaOnAlpha=13`; `dfAlpha=0`, `dfOpaque=1`, `dfMask=2`, `dfProvince=3`,
`dfAddAlpha=4`, `dfAuto=128`.

Healthy title-screen signature:

```
probe-fill face=4 type=12 color=00000000 800x600
probe-composite src=12 dst=1 met=11 opa=255 hda=0 800x600
```

Discriminating outcomes:

- `met=0` — a copy is overwriting the art beneath.
- `met=13` — the unimplemented `bmAddAlphaOnAlpha` path; the parent is `ltAlpha`.
- `face=1` or `color=ff……` — the alpha byte is lost at fill time.
- `probe-exclude` naming a `type=12` layer — exclusion still fires despite §28.
- **no `src=12` line at all** — the message layer never reaches `BltImage`, and
  the cache-bitmap, `Draw_GPU` or transition drawable path owns the bug.

The last outcome is the one that would invalidate the most prior reasoning, and
it is why the probe reports absence as informatively as presence.

### 29.5 Status

Sharin stays `hardware_blocked`. This build is a diagnostic, not a fix, and
must not be recorded as a compatibility claim.

## 30. Sharin: probe round one — every composite decision is correct — 2026-08-17

VPK `9b5d1bac…` ran on hardware. The probe cleared three suspects outright.

### 30.1 Results

```
probe-fill      face=0 type=1  color=ff000000 800x600   base layer, explicit dfAlpha face
probe-fill      face=4 type=12 color=00000000 800x600   title menu layer
probe-fill      face=4 type=12 color=80000000 608x448   message box
probe-composite src=12 dst=1 met=11 opa=255 hda=0 800x372
probe-exclude   type=1 opa=255 imgopaque=0/1 rect=0,0,800,600
```

- **Fills are correct.** `face=4` is `dfAddAlpha` and the ARGB word keeps its
  alpha byte: `0x00000000` for the title, `0x80000000` for the message box.
  The alpha is not being dropped at fill time.
- **Composites are correct.** `src=12 dst=1 met=11` is exactly
  `ltAddAlpha`→`ltOpaque` resolving to `bmAddAlpha`, at `opa=255`, `hda=0`.
- **Exclusions are correct.** Only `type=1` (`ltOpaque`) layers ever claim one.
  No alpha layer excludes, so §28's mechanism is confirmed dead — and the §28
  guard itself is confirmed working, not merely present.

`bmAddAlphaOnAlpha` (§29.3) is never reached. Do not pursue it for Sharin.

### 30.2 The round-one filter was wrong, and it nearly hid this

The composite probe filtered by **size** (>=320x200), so its 48-line budget was
consumed by the `ltAlpha` graphic layers cross-fading through the title
sequence — the `opa=200/150/100/50` ramp visible in the log — and ran out
before the title screen finished. Only three `src=12` lines survived. Filter on
**layer type**, not size, when the question is about a specific layer kind.

### 30.3 What remains: decay under repetition

`TVPAddAlphaBlend_n_a` (`tvpgl.h:88-93`):

```c
tjs_uint32 sopa = (~src) >> 24;
return TVPSaturatedAdd((((dest & 0xff00ff)*sopa >> 8) & 0xff00ff) + ...
```

For a fully transparent source `sopa == 255`, so a channel at 255 becomes
`255*255>>8 == 254`. **Every composite of a transparent layer darkens what is
beneath it by roughly 1/256.** The fixed point is 0, so a few hundred
repetitions reach black.

This is stock Kirikiri arithmetic and is present on Windows, where the title
renders correctly. It is harmless there because the destination is re-copied
from the base layer (`CopySelf` in `InternalDrawNoCache_CPU`) before children
blend onto it. It becomes fatal only if something re-blends onto its **own
previous output**.

That fits all three reported symptoms: the title is a full-screen transparent
`ltAddAlpha` layer over animated `@lightinit` menu art that redraws every
frame, and both message boxes are the same layer kind held on screen.

`tests/test_yuri_arm_alpha.cpp:248` already asserts this decay does not happen
(300 transparent composites over white). That assertion is absolute, not a
NEON-vs-scalar comparison, so it should be failing — §27.2 recorded the probe
as passing, which means either it predates the decay check or the result was
mis-recorded. Re-run `scripts/run-arm-alpha-probe.sh` before trusting §27.2.

### 30.4 Round two

VPK `3366a1fd…`. The composite probe now filters on
`drawtype == ltAddAlpha && destlayertype == ltOpaque`, budgets 200 samples, and
records the source pixel plus the destination pixel **before and after** each
blend (`spx=… dst=before->after`), via a non-mutating `GetPoint`.

- `dst=` before-values walking steadily downward across frames ⇒ the
  destination accumulates; the fault is in the compositor's reuse of its own
  output, and the fix belongs there.
- `dst=` before-values stable while `after` is dark ⇒ the fault is in the
  source content, upstream of the blend.

**Do not "fix" the `>>8` arithmetic to make the symptom go away.** It matches
upstream Kirikiri, which renders this title correctly on Windows; changing it
would mask a real compositor defect and shift every blend result on every
title.

### 30.5 Status

Still `hardware_blocked`. Both VPKs in §29/§30 are diagnostics and must not be
recorded as compatibility claims.

## 31. Sharin: the compositor is innocent — the source pixel is opaque — 2026-08-17

VPK `3366a1fd…` ran on hardware with pixel sampling. This is the first hard
localisation of the defect.

### 31.1 The blend faithfully renders bad input

```
#6  src=12 dst=1 met=11 opa=255 hda=0 800x372 spx=ff000000 dst=ffddffff->ff000000
```

The **source pixel is `ff000000` — alpha 255**. Additive-alpha with a fully
opaque source correctly produces exactly the source, so `met=11` is doing the
right thing with the wrong input. Corroborated by the mixed samples: `#1` and
`#4` carry genuinely opaque non-black sources (`ff1e4f39`, `ffc4cdc9`, the menu
button art) and correctly replace the destination. Only the large message-layer
regions carry `ff000000`.

### 31.2 The decay theory (§30.3) is disproved

Predicted: pre-blend destination drifts downward frame over frame.
Observed: `#1`, `#22`, `#43`, `#44`, `#45` all read `dst=ff489fc8`,
byte-identical across roughly 300 frames.

The destination is re-copied from the base correctly and **nothing
accumulates**. The 1/256 `>>8` loss in `TVPAddAlphaBlend_n_a` is real
arithmetic but is not this bug. Do not "fix" it — §30.4's warning stands, and
it is now backed by measurement rather than caution.

### 31.3 Where the defect actually is

`FillRect` requests `0x00000000` (§30.1) and the compositor reads `0xff000000`.
The alpha byte is lost between those two points. Nothing in the composite path
is implicated any more:

- blend method selection, opacity and hda: correct
- destination refresh from the base: correct
- update exclusion: correct, only `ltOpaque` layers claim one
- draw face and fill colour: correct

### 31.4 A pattern worth keeping

Every large `ltAddAlpha` source region sampled reads `ff000000`, and the only
fills captured for those layers requested `0x80000000` (message box) or
`0x00000000` (title). Both alpha values arrive at the compositor as `0xff`.
That is alpha being **forced opaque**, not corrupted — which points at a
whole-surface property (texture format, allocation, or a copy-on-write alias)
rather than at arithmetic.

Consistent with this: `ltAlpha` graphic layers (`src=2`, loaded through the
image decoder) composite correctly and the user reports images render fine.
The layers that fail are the ones whose bitmaps are created by
`setSize`/`Recreate` rather than by the image loader. That difference is the
first thing to check, along with the project's `IsIndependent()` change (§ the
presentation-reference patch) which sits directly in `Fill`'s copy-on-write
path via `GetTextureForRender` → `Independ()`.

**This is a hypothesis, not a finding.** Two candidate fixes have already been
shipped on reasoning of exactly this quality and both were wrong.

### 31.5 Round three

VPK `80853391…`. Reads the pixel back from the layer's own image immediately
after `MainImage->Fill(destrect, color)` returns, printed as `back=`:

- `back=00000000` against a `00000000` request ⇒ the fill landed; a later write
  opacifies the layer. Hunt the later write.
- `back=ff000000` ⇒ the fill path itself drops alpha, in the render method or
  the texture. Hunt `tTVPRenderMethod_FillARGB`, `GetTextureForRender`,
  `Independ()` and the texture format.

Fill budget raised to 24 because round two's budget of 8 was exhausted by the
`608x448` message-box fills before the title layer's `00000000` clear.

Caveat on the instrument: `GetPoint` reads through the same texture wrapper the
compositor uses, so a "direct texture" path could in principle make both the
probe and the compositor agree on a value the fill never wrote. The composite
reading matches the visible symptom exactly, so the read is trusted for now.

### 31.6 Status

Still `hardware_blocked`. All three VPKs in §29-§31 are diagnostics.

## 32. Sharin: the fill is exonerated; the temp bitmap is the suspect — 2026-08-17

VPK `80853391…` ran on hardware. The read-back closed the bracket opened in
§31.5.

### 32.1 Every fill lands exactly as requested

```
probe-fill #3 face=4 type=12 color=80000000 608x448 back=80000000
probe-fill #9 face=4 type=12 color=00000000 800x600 back=00000000
```

Both the message box's `0x80000000` and the title layer's `0x00000000` read
back byte-identical immediately after `MainImage->Fill` returns. The alpha byte
survives `FillRect`, `tTVPRenderMethod_FillARGB`, the texture, and the
copy-on-write path.

**§31.4 is disproved.** Texture format does not force alpha opaque, and the
project's `IsIndependent()` change is not implicated. Do not re-open either.

### 32.2 Therefore the composite source is not MainImage

Fill lands as `00000000`; the compositor reads `ff000000` (§31.1). Both
readings are trustworthy — same `GetPoint`, same texture wrapper. The only way
both hold is that `BltImage` is not being handed `MainImage`.

`DrawSelf` passes `MainImage` straight to the parent only when a layer has no
visible children (`LayerIntf.cpp:5878`). The title menu layer has one
`ButtonLayer` child per menu entry — `@button graphic=t_next_chap/t_start/
t_save/t_load/t_config/t_exit` in `scenario/title2.ks` — so it takes
`InternalDrawNoCache_CPU`'s other path: a **shared temp bitmap** from
`tTVPTempBitmapHolder::GetTemp`, which `CopySelf` is supposed to fill with
`MainImage` before the children draw into it.

That also explains the composites that look right: `#1` and `#4`
(`spx=ff1e4f39`, `ffc4cdc9`, 178x39-ish) are the opaque button art, which is
correct whatever the temp bitmap held.

### 32.3 The branch to look at

`tTJSNI_BaseLayer::CopySelf` (`LayerIntf.cpp:5913`) skips the copy outright
when `UpdateExcludeRect` covers the target region:

```cpp
else if(r.left >= uer.left && r.right <= uer.right)
{
    ;// nothing to do
}
```

A skip leaves the shared temp bitmap holding **the previous user's pixels**.
The probe log does show full-screen `rect=0,0,800,600` exclusions being
claimed, so a non-empty `UpdateExcludeRect` reaching a message layer is not
hypothetical.

Note the ordering that makes this survivable in stock Kirikiri:
`QueryUpdateExcludeRect` visits children backward (topmost first) and a layer
adds its own opaque area to `rect` only *after* recursing, so a parent never
excludes its own children. Whether that invariant still holds here is exactly
what round four measures.

### 32.4 Round four

VPK `aa53117b…`. Two probes, both filtered to `ltAddAlpha`:

- `probe-copyself #N SKIPPED type=12 uer=… r=…` — the copy was skipped; the
  temp bitmap is stale. Prints both rectangles so the exclusion that caused it
  can be identified.
- `probe-copyself #N copied type=12 back=…` — the copy ran. `back=00000000`
  means faithful and something later opacifies the temp bitmap; `back=ff000000`
  means the copy itself loses alpha.

### 32.5 Discipline note

This is the fourth mechanism in this investigation that "explains the
screenshot exactly". Three of the previous three were wrong (§28 stale
IsOpaque, §30.3 blend decay, §31.4 texture format). It is instrumented rather
than fixed for that reason. Ship a fix only against a `probe-copyself` line.

### 32.6 Status

Still `hardware_blocked`. All four VPKs in §29-§32 are diagnostics.

## 33. Sharin: three measurements that cannot all describe one bitmap — 2026-08-17

VPK `aa53117b…` (round four) ran on hardware. The VPK on disk was verified to
be that build, so the result below is real and not a stale install.

### 33.1 The contradiction

1. `probe-fill … back=00000000` — the fill reads back correctly from MainImage.
2. `probe-composite … spx=ff000000` — the composite reads an opaque source.
3. **No `probe-copyself` lines at all** — `CopySelf` is never reached for an
   `ltAddAlpha` layer.

`CopySelf` is the only thing that can interpose a surface other than MainImage
between (1) and (2). If (3) holds then the composite source *is* MainImage, and
(1) and (2) contradict each other.

**§32 is disproved.** The temp-bitmap theory required `CopySelf` to run. Do not
re-open it, and do not re-derive it from `GetTemp` being uncleared — that is
true but irrelevant if nothing copies into it.

### 33.2 The untested assumption underneath four rounds

Every round so far has assumed the fill probe and the composite probe were
watching the same layer. That was never checked. KAG keeps two message layers
per index — `fore.messages[i]` and `back.messages[i]`
(`MainWindow.tjs:2825-2826`) — and `title2.ks:53-56` draws the title menu on
**page=back**:

```
@layopt layer=message1 page=back visible=true
@current layer=message1 page=back
```

So "the fill lands correctly" and "the composite reads opaque" may both be true
of *different* bitmaps.

### 33.3 Round six

VPK `ad71fe03…`.

- `probe-fill … surf=XXXXXXXX` — address of the filled MainImage.
- `probe-composite … surf=XXXXXXXX` — address of the composited src bitmap.
  Equal ⇒ one surface, opacified between the two events. Different ⇒ the
  compositor reads a bitmap nothing ever cleared, and §30-§32's "the fill is
  correct" conclusions were about the wrong object.
- `probe-copyself-entry` — logs **every** CopySelf call, no type or size
  filter, so an absence is a statement about CopySelf and not about the filter.
- `probe-build r6 surface-identity` — printed at startup from the render-task
  pool init.

### 33.4 Process failures worth not repeating

- **No build stamp for five rounds.** When round four returned an unexpected
  negative the first instinct was to doubt the install, and the user was asked
  to vouch for it. They were right; the VPK on disk was round four. A stamp
  costs one line and removes the whole class of doubt. Every probe build must
  carry one.
- **A filtered probe's silence was read as evidence.** `probe-copyself` was
  filtered on `ltAddAlpha` *and* on size >= 320x200. Silence from a filtered
  probe cannot distinguish "did not happen" from "did not match". Always pair a
  filtered probe with an unfiltered entry counter.
- **A polling `while pgrep …` loop was left running for 1h40m** despite the
  user having already objected to that pattern once. Use background tasks and
  wait for the completion notification.

### 33.5 Status

Still `hardware_blocked`. Five diagnostic VPKs so far (§29-§33) and no fix. The
§32 CopySelf copy-anyway change remains in the tree but is inert — that branch
is not being reached.

## 34. Sharin: same bitmap confirmed; switch to event-triggered probing — 2026-08-17

VPK `ad71fe03…` (round six, stamped `probe-build r6 surface-identity`).

### 34.1 The fill and the composite touch the same bitmap

```
probe-fill      #9 face=4 type=12 color=00000000 800x600 back=00000000 surf=835ec410
probe-composite #6 src=12 dst=1 met=11 800x372 spx=ff000000 dst=ffddffff->ff000000 surf=835ec410
```

`835ec410` in both. **§33's fore/back page theory is disproved.** The layer's
own MainImage is written transparent and read opaque; something opacifies it in
between.

Supporting detail: `surf=835ec410` is filled at 608x448 (`#8`) and then at
800x600 (`#9`) — the same layer resized by `@position width=800 height=600`,
matching `title2.ks:56`.

### 34.2 CopySelf is definitively not involved

All 16 `probe-copyself-entry` lines are `type=1`. The unfiltered counter proves
`CopySelf` never runs for an `ltAddAlpha` layer, so §32's silence was real and
not a filter artefact. Composites `#1`-`#5` share `surf=83d654b0` across five
different rect sizes — one pooled temp bitmap, confirming `GetTemp` reuse
exists but is not on the message layer's path. `#6` uses the MainImage address
directly, so the message layer takes `DrawSelf` → `MainImage`.

### 34.3 Why rounds three to six were the wrong shape

Each picked one candidate writer — CopySelf, texture format, the temp bitmap,
the other KAG page — instrumented only that, and was wrong. Each wrong guess
costs the user a transfer, a VitaShell install, a run, and a log copy. The
method was at fault, not the luck.

### 34.4 Round eight: probe the event, not the suspect

VPK `ad9c5148fde00d627f3c5185bc450cf4f2f13db773358e84625348e016a7fec3`
(stamp `probe-build r8 raster-funnel`).

`iTVPRenderManager` has exactly three public raster entry points, and all three
are now wrapped in a scoped `krkrvita::RasterProbe`:

- `OperateRect` (`RenderManager.cpp:2816`) — Fill, CopyRect, Blt, text blits
- `OperateTriangles` (`:3122`) — stretched draws
- `OperatePerspective` (`:4270`) — perspective draws

The probe samples the target's centre pixel before and after, and logs only
when the defect actually occurs: **a target at least 320x200 whose centre goes
from non-opaque to opaque**. It reports `method->GetName()`, so the culprit
names itself. Normal drawing costs two pixel reads and logs nothing.

Because all three entry points are covered, an empty result is also
informative: it would mean the write does not go through the software render
manager at all, which points outside the renderer.

### 34.5 Two build-system traps hit while wiring this

- **A competing overlay is silent.** The first attempt wrote its own
  `${yuri_generated_dir}/RenderManager.cpp`; an existing later patch
  (`YuriBackend.cmake`, texture-recycler chain) overwrote it with no error, so
  the probe would have been absent from the binary and the empty log misread as
  "nothing opacifies it". Always chain into an existing overlay, and always
  verify with `grep -a` on the generated file.
- **Verify the probe reached the ELF**, not just the generated source:
  `strings build-vita-yuri/krkrvita-yuri | grep probe-`.

### 34.6 Status

Still `hardware_blocked`. No fix has been shipped since §28; every VPK in
§29-§34 is a diagnostic.

## 35. Sharin: host harness built; §34's reading of probe-raster was wrong — 2026-08-17

### 35.1 A correction that matters

§34 read `probe-raster #3 FillARGB 80000000->ff000000 608x448` as "the message
layer is being overwritten with opaque black", on the grounds that `before`
matched the value just written to that layer.

**That inference does not hold.** The host harness reproduces the identical
signature while every pixel assertion passes:

```
probe-raster #1 FillARGB 00000000->ff000000 800x600 tex=4bb3dcd0
probe-raster #4 FillARGB 80000000->ff000000 608x448 tex=4bb3dcd0
```

Same texture address at two different sizes: memory freed and reallocated. A
`before` value of `80000000` on a freshly allocated texture is **stale heap
content**, not evidence that a live layer was corrupted. `probe-raster` reports
the target texture, so it cannot establish surface identity, and every
conclusion in §34.1-§34.4 that leaned on it is withdrawn.

### 35.2 What still stands

The one hardware pair that does establish identity, because both probes report
the same `iTVPBaseBitmap*`:

```
probe-fill       #9 color=00000000 800x600 back=00000000 surf=83971538
probe-draw-entry #1 type=12 centre=ff000000 800x600 surf=83971538
```

Note carefully what this does and does not prove. `surf` is the **bitmap
wrapper**, whose internal texture pointer is replaced by `Independ()`,
`Recreate()`, `SetSize()` and `AssignTexture()`. "Same surf, different content"
is therefore equally consistent with:

- the layer's pixels being overwritten in place, or
- the layer's bitmap being pointed at a **different texture** whose content is
  opaque black.

The second reading is untested and is the more likely of the two, because a
plain in-place overwrite would have to come from somewhere and none of the
enumerated `Fill(0xff000000)` sites target a message layer.

### 35.3 The harness

`scripts/run-texture-aliasing-harness.sh`, with
`tests/test_yuri_texture_aliasing.cpp` and
`tests/yuri_texture_harness_stubs.cpp`.

It links the **generated** `RenderManager.cpp`, `RenderManager.h` and
`LayerBitmapIntf.cpp` from `build-vita-yuri`, not the vendor originals, so the
code under test includes this project's patched
`iTVPTexture2D::IsIndependent()`. An earlier version of the harness used the
vendor headers, exercised stock `RefCount == 1` semantics, and passed for the
wrong reason — check this first if the harness ever looks too green.

Current result: all four checks pass. Plain texture aliasing between
independent bitmaps, across recycler drains, and through the full-area
`CopyRect` → `AssignTexture` sharing path, is **not** the defect.

Stubs live in `yuri_texture_harness_stubs.cpp` and abort rather than returning
plausible values, so a stub that starts being reached fails loudly. Two
exceptions, both deliberate: `TVPExecThreadTask` runs tasks inline (a faithful
single-core execution) and `TVPGetDefaultFontName` answers, because bitmap
construction always has a font name in the real engine.

`TVPInitTVPGL()` must be called before any fill or blend; without it every
TVPGL entry point is a null pointer and the first `Fill` segfaults.

### 35.4 Why this is the right investment

Eight hardware rounds each cost the user a transfer, a VitaShell install, a
run and a log copy, and each could eliminate at most one hypothesis. The next
hypotheses -- texture swap via `Independ`/`Recreate`/`AssignTexture`, and the
`IsIndependent()` relaxation interacting with deferred deletion -- are all
testable here in seconds. Extend `test_yuri_texture_aliasing.cpp`; do not ask
for another probe VPK.

### 35.5 Status

Still `hardware_blocked`, still no fix. The next device run should be a fix
candidate verified against this harness first.

## 36. Sharin: texture lifetime invariant fixed — 2026-08-17

The host harness found a genuine use-after-free in the deferred texture
recycler, reproduced it, and verified the fix. No device run was needed to get
this far.

### 36.1 The defect

`RenderManager.cpp`:

```cpp
void iTVPTexture2D::Release() {
	if (RefCount == 1)
		_toDeleteTextures.push_back(this);   // RefCount stays 1
	else
		--RefCount;
}

void iTVPTexture2D::RecycleProcess() {
	for (iTVPTexture2D* tex : pending) { delete tex; ... }   // unconditional
}
```

A queued texture still reports `RefCount == 1`. That means it still looks
exclusively owned to `IsIndependent()`, still accepts `AddRef()`, and still
takes the `RefCount == 1` branch on a second `Release()`. Two consequences:

- **Double queue → double delete.** Releasing an already-queued texture pushes
  it again; the drain deletes it twice and corrupts the allocator.
- **Resurrect → use-after-free.** Anything acquiring the texture between the
  queueing and the drain is left holding freed memory.

Both end with one heap block owned by two live objects. `tTVPLayerManager`
clears its draw buffer to `0xFF000000` (`LayerManager.cpp:102`, `:112`, `:139`,
`:149`, `:163`) and nothing else in the visual tree fills with that value, so a
layer bitmap sharing that block reads **opaque black** — the reported symptom.

### 36.2 Reproduced, then fixed

`tests/test_yuri_texture_aliasing.cpp` gained two tests modelling the hazards
at the texture level. Against the unpatched engine the harness **segfaults**;
against the fix all checks pass. That is a reproduction, not an argument.

The fix makes "queued" a distinct state:

- `Release()` sets `RefCount = 0` when queueing, so a texture cannot be queued
  twice, and a further release is refused and traced.
- `RecycleProcess()` skips any texture whose `RefCount > 0`, so a resurrected
  texture is kept and simply re-queued on its final release.
- `IsIndependent()` reports false at `RefCount == 0`
  (`yuri_texture_has_single_mutable_owner` requires `total > 0`), so a dead
  texture is never mistaken for an exclusively owned one and copy-on-write
  stays conservative.

### 36.3 Honest scope

The invariant is now sound and the use-after-free is demonstrably gone. What is
**not** proven is that Sharin's black layers were caused by this specific path
— the harness shows the hazard is reachable, not that the engine reaches it
while rendering that title.

Two boot traces settle that on the next run, and make it useful either way:

- `yuri-texture-double-release`
- `yuri-texture-resurrected-after-queue`

If either appears in `boot-status.txt`, the engine was hitting the bug and the
fix is preventing it. If neither appears and the layers are still black, this
was a real defect but not *the* defect, and the search continues with the
harness rather than with device probes.

### 36.4 Status

Sharin stays `hardware_blocked` until a device run confirms. This is the first
fix candidate since §28 and the first one derived from a reproduction instead
of a hypothesis.

## 37. Sharin transparency: consolidated dead-end register — 2026-08-17

**Read this before touching the Sharin black-layer defect.** Nine hardware
rounds and five distinct fix attempts are recorded below. Every one is
disproved *with evidence*, not merely "tried and didn't work". Re-attempting
any of them wastes a physical-device cycle: transfer, VitaShell install, run,
collect log.

### 37.1 The symptom, stated precisely

`車輪の国、向日葵の少女` boots, loads images correctly, and enters the
scenario. Layers that composite through an alpha channel render as **solid
black**: the title menu (a full-screen `ltAddAlpha` message layer at
`frameOpacity 0`), and message boxes (the same layer kind at `frameOpacity
128`). Content drawn *onto* those layers — text, green link highlights, button
art — renders correctly. The art beneath them is replaced by black.

### 37.2 Disproved fixes — do not retry

| # | Hypothesis | How it was disproved |
| --- | --- | --- |
| 1 | Stale `MainImage->IsOpaque()` widens `QueryUpdateExcludeRect`, erasing layers beneath (§28) | Guard shipped and confirmed in the built source; black survived. `probe-exclude` shows only `type=1` (`ltOpaque`) layers ever claim an exclusion. |
| 2 | `TVPAddAlphaBlend_n_a`'s `dest*sopa>>8` loses 1/256 per composite, decaying to black (§30.3) | `probe-composite` pre-blend destination is **byte-identical** across ~300 frames (`dst=ff489fc8` at samples #1/#22/#43/#44/#45). Nothing accumulates. |
| 3 | Texture format or the patched `IsIndependent()` forces alpha opaque at fill time (§31.4) | `probe-fill … back=` reads the pixel straight back after `MainImage->Fill`. It is always exactly what was requested: `80000000`, `00000000`. |
| 4 | `CopySelf` skips its copy under `UpdateExcludeRect`, leaving a stale shared temp bitmap (§32) | `probe-copyself-entry`, unfiltered, fires 16 times and **all are `type=1`**. `CopySelf` is never reached for an `ltAddAlpha` layer. |
| 5 | The fill and the composite are looking at different KAG pages (`fore` vs `back`) (§33) | Both probes report the same `iTVPBaseBitmap*`: `surf=835ec410`. One bitmap. |
| 6 | Deferred-recycler use-after-free aliases a layer onto the draw buffer (§36) | A genuine bug, reproduced and fixed (harness segfaults before, passes after). **Sharin is still black**, so it was not this defect. Keep the fix; it is correct on its own merits. |

### 37.3 Also eliminated, with evidence

- **Blend arithmetic on real ARM.** `scripts/run-arm-alpha-probe.sh`: all five
  variants colour-exact against scalar TVPGL on hardware (§27.2).
- **Framebuffer alpha reaching the screen.** `present_bound_texture()` uses
  `GL_REPLACE`; `configure_2d()` calls `glDisable(GL_BLEND)`
  (`vitagl_presenter.cpp:131`). Destination alpha cannot darken the frame.
- **Blend selection.** `probe-composite` reports `src=12 dst=1 met=11 opa=255
  hda=0` — `ltAddAlpha`→`ltOpaque` resolving to `bmAddAlpha`, correct.
- **Draw-face selection.** `UpdateDrawFace` maps `ltAddAlpha`→`dfAddAlpha`
  (`LayerIntf.cpp:1394`); `probe-fill` confirms `face=4` at run time.
- **`bmAddAlphaOnAlpha` being unimplemented** (`RenderManager.cpp:2359`
  returns `nullptr`). Real, but never reached: the message layers' parent is
  `ltCoverRect`/`ltOpaque` (`GraphicLayer.tjs:336`).
- **`tTVPDestTexture::CopyRect` sharing a texture** — it forces
  `TVP_BB_COPY_MAIN`, which skips the `AssignTexture` fast path entirely.
- **Presentation references touching layer images** — `AddPresentationRef` is
  called only on the draw buffer's texture (`yuri_window_layer.cpp:185`).

### 37.4 The one solid fact still unexplained

```
probe-fill       #9 color=00000000 800x600 back=00000000 surf=835ec410
probe-draw-entry #1 type=12 centre=ff000000 800x600 surf=835ec410
```

The same bitmap wrapper is filled transparent and, on entry to `Draw()`, reads
opaque black. Note what this does **not** pin down: `surf` is the
`iTVPBaseBitmap*`, whose internal texture pointer is replaced by `Independ()`,
`Recreate()`, `SetSize()` and `AssignTexture()`. So this is equally consistent
with the pixels being overwritten *and* with the bitmap being repointed at a
different, opaque texture. That distinction is the next thing to settle.

### 37.5 Instrumentation traps that produced false conclusions

- **`grep` silently skips Yuri's Shift-JIS sources.** `grep -n IsOpaque` on
  `LayerIntf.cpp` prints nothing whether or not a patch applied. Always
  `grep -a`. This caused two wrong conclusions (§7, §29.1).
- **A filtered probe's silence proves nothing.** `probe-copyself` filtered on
  layer type *and* size; its silence was read as "CopySelf does not run" when
  it could equally have been "the filter did not match". Always pair a filtered
  probe with an unfiltered entry counter.
- **`probe-fill` and `probe-raster` filter on different things** — destination
  rect vs texture size — so "no fill line" does not mean "not from FillRect".
- **`probe-raster`'s `before` value does not identify a surface.** The host
  harness reproduces `80000000->ff000000` while every assertion passes: it is
  stale heap in a freshly reallocated block (§35.1).
- **A competing CMake overlay is silent.** Writing your own
  `${yuri_generated_dir}/RenderManager.cpp` when a later patch also writes it
  means your probe is absent from the binary with no error. Chain into the
  existing overlay, verify with `grep -a` on the generated file, and confirm
  with `strings` on the ELF.
- **Ship a build stamp in every probe build.** Five rounds had none; when round
  four returned an unexpected negative the user was asked to vouch for their
  install, and they were right.

### 37.6 Tooling that now exists

- `scripts/run-texture-aliasing-harness.sh` — builds and runs Yuri's real
  bitmap/texture/render code **natively**, in seconds, linking the *generated*
  Vita sources so the product's own patches are under test. This is where the
  next hypothesis should be tested. Do not spend a device cycle on anything
  reproducible here.
- `scripts/run-arm-alpha-probe.sh` — TVPGL scalar-vs-NEON differential on the
  Cortex-A9 board.
- `include/krkrvita/yuri_composite_probe.hpp` plus the `YuriBackend.cmake`
  injections — fill, composite, exclude, copyself, draw-entry and raster-funnel
  probes, all capped and stamped.

### 37.7 Status

`車輪の国、向日葵の少女` remains **`hardware_blocked`**. It must not be
recorded as a positive compatibility claim. Nine device runs have produced no
fix for this defect; the §36 texture-lifetime fix is real and worth keeping but
addresses a different bug.

## 38. Sharin: the compositor now runs on the host — 2026-08-17

The §36 texture-lifetime fix did **not** clear Sharin (see §37.2 row 6). Rather
than spend a tenth device cycle, the harness was extended to cover the layer
stack itself.

### 38.1 What now runs natively

`scripts/run-texture-aliasing-harness.sh` links, in addition to the bitmap and
render layer:

- `LayerIntf.cpp` — `tTJSNI_BaseLayer`, the whole layer/compositing
  implementation
- `LayerManager.cpp` — `tTVPLayerManager` and its `DrawBuffer`
- `TransIntf.cpp`, `MsgIntf.cpp`

all taken from `build-vita-yuri/generated/yuri` where a generated version
exists, so the product's own patches are what execute.

Three facts made this possible and are worth recording, because each looked
like a blocker:

- Both units compile for the host with **zero** errors; only two include paths
  were missing (ffmpeg, freetype2).
- `tTJSNI_BaseLayer` has **no pure virtuals** and its default constructor only
  zeroes members — `Owner`, `Manager` and `Parent` all start `NULL`. A layer
  can therefore be instantiated directly, without a TJS engine, window object
  or native-class registration.
- The link closure was only 61 symbols, 36 of them message-string globals that
  `msg/MsgIntf.cpp` already defines properly as `tTJSMessageHolder`. Do not
  hand-declare those as `ttstr`; it is a type conflict. Link `MsgIntf.cpp`.

`tests/yuri_layer_harness_stubs.cpp` supplies the remainder: graphics loading,
event dispatch, cursors and the script-facing `tTJSNC_Layer::CreateNativeInstance`.
`TVPToActualColor` / `TVPFromActualColor` are implemented for real (identity,
as Kirikiri does without a palette) because they are on the fill path.

### 38.2 Why this matters more than another probe

The remaining question from §37.4 — whether the layer's *pixels* are
overwritten or its *bitmap is repointed at a different texture* — is directly
observable here: construct a primary `ltOpaque` layer with an `ltAddAlpha`
child, fill the child, run the draw cycle, and read both the pixel and
`MainImage->GetTexture()` before and after. That is one afternoon of harness
work versus an unbounded number of device cycles.

### 38.3 Next step

Write that reproduction in `tests/test_yuri_texture_aliasing.cpp`. The existing
tests already pass against the layer-linked build, so the scaffolding is sound;
what is missing is a layer-tree test, not more infrastructure.

### 38.4 Status

`車輪の国、向日葵の少女` remains **`hardware_blocked`**. Nine device runs, no
fix. Do not ask the user for a tenth until a reproduction exists here.

## 39. Sharin: layer-tree reproduction built — does NOT reproduce — 2026-08-17

`tests/test_yuri_layer_composite.cpp`, run via
`KRKRVITA_HARNESS_TEST=tests/test_yuri_layer_composite.cpp
scripts/run-texture-aliasing-harness.sh`.

It builds Sharin's actual tree — an `ltOpaque` primary holding background art
with a full-screen `ltAddAlpha` child cleared to `0x00000000`, which is what
`@position left=0 top=0 width=800 height=600 frame="" opacity=0` produces —
attaches a real `tTVPLayerManager`, and runs `UpdateToDrawDevice()`.

### 39.1 Result

```
message layer:
  after fill             pixel=00000000 texture=0x...9430
  after draw cycle       pixel=00000000 texture=0x...9430
  draw buffer            pixel=ffbf7f1f
```

**The minimal tree composites correctly.** The layer stays transparent, its
texture pointer is unchanged, and the background art (`ffc08020`) survives to
the draw buffer as `ffbf7f1f`.

Two things worth extracting:

- The **basic path is sound**: `ltAddAlpha` transparent over `ltOpaque`,
  through the real compositor and the real draw buffer, does not blacken
  anything. Whatever breaks on device is not this.
- `ffc08020` → `ffbf7f1f` is the 1/256 loss from `TVPAddAlphaBlend_n_a`,
  visible in a single pass. Harmless here, and §37.2 row 2 already showed it
  does not accumulate on device. Still not the bug.

### 39.2 The one crisp difference from hardware

```
device: probe-draw-entry #1 type=12 centre=ff000000 800x600
host:   probe-draw-entry #1 type=12 centre=00000000 800x600
```

On device the layer is **already opaque when Draw() begins**. On host it is
still transparent. So the corruption happens *before* the draw cycle, and the
compositor is exonerated — consistent with §37.2 rows 1, 4 and 5, and now shown
positively rather than by elimination.

### 39.3 What the minimal tree does not model

The corruption therefore needs something the reproduction lacks. In rough order
of suspicion:

1. **The resize.** On device the layer is filled at 608x448, resized to
   800x600 by `@position`, then filled again. `ChangeImageSize` →
   `SetSizeWithFill(w, h, NeutralColor)` fills only the *expanded strips*
   (`LayerBitmapIntf.cpp:131-146`) and relies on `SetSize` preserving the rest.
2. **Children.** The real message layer has one `ButtonLayer` per menu entry
   plus `lineLayer` and `highlightLayer`, which pushes it onto
   `InternalDrawNoCache_CPU`'s temp-bitmap path instead of `DrawSelf`.
3. **Siblings and pages.** `back.base` is a second full-screen `ltOpaque` child
   of `fore.base`, and each page has its own message layers.
4. **Allocation churn.** Four message layers, repeated clears, transitions.

Add these one at a time to `test_yuri_layer_composite.cpp` and bisect. The
scaffolding is done; each addition is a few lines.

### 39.4 Harness notes

`test_yuri_layer_composite.cpp` opens access with `#define private public`
around `LayerIntf.h`/`LayerManager.h`, because `Manager`, `Draw()` and
`CompleteForWindow()` are private with `tTVPLayerManager` as friend, and
`~tTVPLayerManager` is private for reference counting. Normal construction runs
through `tTJSNI_BaseLayer::Construct`, which needs a TJS window object and
native-class registration that do not exist here and do not affect
compositing. Access control changes neither layout nor mangling, so the other
translation units link against identical code. Confined to that one file.

### 39.5 Status

`車輪の国、向日葵の少女` remains **`hardware_blocked`**. Nine device runs, no
fix. The next step is bisection in the harness (§39.3), not a tenth run.
