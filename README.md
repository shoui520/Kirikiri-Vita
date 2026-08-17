# Kirikiri Vita

Kirikiri Vita is a PlayStation Vita backend port of
[Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) for legally
owned retail Kirikiri visual novels. Retail compatibility, including
`xp3filter.tjs`, is the primary design constraint.

The engine target is Yuri's coherent TJS, storage/archive, native-plugin,
window/layer, bitmap/render and sound backend. The Android/Cocos application
frontend is not being ported. Vita-specific code ends at narrow platform
adapters for files, clocks/threads, input, FreeType fonts, sound output and
VitaGL presentation. Kirikiri SDL2 remains a source reference for newer
portable fixes where their semantics are compatible; it is not the retail
engine base.

The only Vita runtime produced by the root build is the standalone Yuri backend
at `build-vita-yuri/krkrvita-yuri.vpk`. The obsolete SDL bring-up target is not
part of the release path. Yuri's `layerExMovie.dll` still requires a port to
VitaSDK's current FFmpeg API; the milestone game does not contain movie media.
Exact native-module status is tracked in `docs/YURI-COMPATIBILITY.md`.

## Current development commands

```sh
git submodule update --init --recursive
cmake -S . -B build-host -DKRKRVITA_BUILD_TESTS=ON
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure -j1
./build-host/krkrvita-tool scan /path/to/game
```

The local final-compatibility corpus is pinned in
`tests/retail_compatibility_manifest.txt`. Its workstation gate fingerprints
each title, reruns Phase 1, opens every XP3, decodes every script, compiles all
TJS and KAG inline-script blocks, checks boot reachability, signature-checks
every supported image/audio payload, rejects unsupported runtime formats both
inside XP3s and next to the executable, and validates literal plug-in requests
against the same capability inventory used by the Vita loader:

```sh
ctest --test-dir build-host -L compatibility -j2 --output-on-failure
./scripts/run-retail-compatibility.sh
./scripts/run-retail-compatibility.sh --require-all
cmake --build build-host --target krkrvita-final-compatibility-gate
```

The last two commands are intentionally product gates. They run every manifest
row before returning and remain nonzero for an unsupported plug-in/format,
invalid decoded payload, missing corpus directory, changed fingerprint, or any
title still requiring Phase 2. DeepOne and walpurgis currently require Phase 2.
Optional/caught and dormant plug-in references are reported separately; a host
static audit is not represented as a substitute for a physical-Vita smoke run.
The exact gate semantics, current 19-title matrix and promotion workflow are in
[`docs/RETAIL-COMPATIBILITY-GATE.md`](docs/RETAIL-COMPATIBILITY-GATE.md).

Optional Cortex-A9 validation never copies games to the ARM board. It emits
only each currently compatible title's already-decoded `startup.tjs`, builds a static ARMv7
probe, enforces a flat 64-file/8-MiB bundle limit, rejects archives,
executables, DLLs, media and save files, and removes the remote staging area
after the run:

```sh
./scripts/run-retail-matrix-on-cortex-a9.sh
```

For Vita:

```sh
VITASDK=/home/shoui/vitasdk ./scripts/build-vita.sh
```

The script is the release gate. It runs the ordinary and ASan/UBSan host suites
against the exact retail fixture, then cross-builds the root Yuri backend and
checks its generated sources, linked symbols, package identity, assets and
embedded patch bundle. It defaults to two build jobs and runs tests serially.
No Android/Cocos or SDL frontend is part of the target.

The Vita must have `ur0:/data/libshacccg.suprx`, as required by upstream
VitaGL. Install it legally with ShaRKBR33D or VitaDB Downloader before launching
Kirikiri Vita. The runtime checks for it before initializing VitaGL and writes a
specific error instead of entering the renderer when it is absent.

The primary retail font is an externally supplied `msgothic.ttc` at
`ux0:data/krkrvita/msgothic.ttc`. The engine selects TTC face 0,
`ＭＳ ゴシック` (MS Gothic), and renders it through Yuri's original FreeType
path without the Vita PVF size or baseline calibration. The font is not placed
in the VPK or redistributed by the project. This personal-use build treats the
font as required and exits with an explicit error when face 0 cannot be opened;
the Yuri executable does not contain an ScePvf fallback.

To prepare a retail directory without modifying it:

```sh
./build-host/krkrvita-tool vita-stage \
  /path/to/game .cache/patches .cache/vita-stage \
  'ux0:data/krkrvita/games/Game Name'
```

Copy the game to the printed `game_copy_to` path and copy the contents of
`.cache/vita-stage` to `ux0:data/krkrvita`. The command fetches the pinned patch
manifest, resolves the game, downloads its complete supported bundle, verifies
`xp3filter.tjs` against the game's XP3 data, and writes both the active and
stable per-game profiles.

To exercise archive-only Phase 1 without consulting the manifest, network,
game executable, or a local hand-written filter:

```sh
./build-host/krkrvita-tool prepare-heuristic /path/to/game .cache/heuristic
```

The command either writes and verifies a synthesized filter or reports that
the supplied game's EXE must be analyzed by the separate Phase-2 pipeline.
Phase 2 is currently deferred; its executable-analysis design and resumption
roadmap are documented in
[`docs/PHASE-2-EXECUTABLE-ANALYSIS.md`](docs/PHASE-2-EXECUTABLE-ANALYSIS.md).

For a single game, the central application can boot an untouched directory
placed directly below `ux0:data/krkrvita/games`. It resolves the exact game
against the pinned Kirikiroid2 patch manifest and extracts only its selected
`patch.tjs` and `xp3filter.tjs` into a revisioned cache. A game-local filter is
the fallback/override, and a staged profile can explicitly select either path.

Every launch recreates `ux0:data/krkrvita/boot-status.txt` before C++ global
constructors run. Fatal startup errors are written with raw Vita I/O to
`ux0:data/krkrvita/error.txt`; both files have root-level fallback names below
`ux0:data` if the application directory cannot be opened. Successful hardware
milestones include startup-script completion, first-window creation, first/60th/
300th game-frame presentation, OpenAL initialization, first queued audio buffer
and first started audio source. These markers distinguish linking from actual
engine, rendering and audio progress.

After a run of at least 30 seconds, copy `boot-status.txt` back to the host and
validate every hardware gate, including the pthread regression test and the
sample's native plug-ins:

```sh
./scripts/verify-vita-run.sh /path/to/boot-status.txt
```

Direct-bubble packaging is disabled until its SFO and installation lifecycle
have passed hardware validation. Do not install previously generated direct
bubbles; use the central `KRVITA001` application.

See `docs/ARCHITECTURE.md` and `docs/MILESTONE-1.md` for supported and pending
functionality. The patch library is fetched at runtime and is not redistributed
by this repository.
