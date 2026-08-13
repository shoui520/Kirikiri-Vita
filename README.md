# Kirikiri Vita

Kirikiri Vita is a PlayStation Vita port of the complete current Kirikiri core
for legally owned retail Kirikiri visual novels. Retail compatibility,
including Yuri-compatible `xp3filter.tjs`, is the primary design constraint.

The engine combines the current Kirikiri core from
[Kirikiri SDL2](https://github.com/krkrsdl2/krkrsdl2) with the retail native API
contract from
[Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri). Yuri's
internal `Plugins.link()` registry and its portable compatibility modules are
compiled into the Vita executable, including `xp3filter.dll` and
`addFont.dll`. Presentation is VitaGL only; the SDL GXM, PIB and PVR renderers
are disabled. Text uses bundled fonts through FreeType and falls back to the
Vita Japanese system font through ScePvf. Audio uses the upstream sound stack
over SDL's Vita audio backend, and controller/touch events support per-game
remapping.

This is not yet arbitrary-game complete: the SDL base's video overlay is a
Windows-only implementation, and Yuri's `layerExMovie.dll` requires a port of
its older FFmpeg player to VitaSDK's current FFmpeg API. The exact native-module
status is tracked in `docs/YURI-COMPATIBILITY.md`.

## Current development commands

```sh
git submodule update --init --recursive
cmake -S . -B build-host -DKRKRVITA_BUILD_TESTS=ON
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
./build-host/krkrvita-tool scan /path/to/game
```

For Vita:

```sh
VITASDK=/home/shoui/vitasdk ./scripts/build-vita.sh
```

The script applies the idempotent patches in `patches/` to pinned recursive
submodules and produces `build-vita-engine/krkrsdl2.vpk`. Do not configure the
old root Vita target; the wholesale engine target is the product runtime.

The Vita must have `ur0:/data/libshacccg.suprx`, as required by upstream
VitaGL. Install it legally with ShaRKBR33D or VitaDB Downloader before launching
Kirikiri Vita. The runtime checks for it before initializing VitaGL and writes a
specific error instead of entering the renderer when it is absent.

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

For a single game, the central application can also boot an untouched directory
placed directly below `ux0:data/krkrvita/games`. It prefers a game-local
`xp3filter.tjs`, then uses the bundled common hash-XOR filter. A staged profile
remains the route for automatically selected game-specific filters and patches.

Every launch recreates `ux0:data/krkrvita/boot-status.txt` before C++ global
constructors run. Fatal startup errors are written with raw Vita I/O to
`ux0:data/krkrvita/error.txt`; both files have root-level fallback names below
`ux0:data` if the application directory cannot be opened.

Direct-bubble packaging is disabled until its SFO and installation lifecycle
have passed hardware validation. Do not install previously generated direct
bubbles; use the central `KRVITA001` application.

See `docs/ARCHITECTURE.md` and `docs/MILESTONE-1.md` for supported and pending
functionality. The patch library is fetched at runtime and is not redistributed
by this repository.
