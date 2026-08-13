# Kirikiri Vita

Kirikiri Vita is a PlayStation Vita port of the complete current Kirikiri core
for legally owned retail Kirikiri visual novels. Retail compatibility,
including Yuri-compatible `xp3filter.tjs`, is the primary design constraint.

The full engine comes from current
[Kirikiri SDL2](https://github.com/krkrsdl2/krkrsdl2). Retail archive filtering
is ported from [Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri),
including its native `xp3filter` plugin ABI. Presentation is VitaGL only; the
SDL GXM, PIB and PVR renderers are disabled. Text uses the Vita Japanese system
font through ScePvf, audio uses the complete upstream sound stack over SDL's
Vita audio backend, and controller/touch events support per-game remapping.

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

Direct-bubble packaging is disabled until its SFO and installation lifecycle
have passed hardware validation. Do not install previously generated direct
bubbles; use the central `KRVITA001` application.

See `docs/ARCHITECTURE.md` and `docs/MILESTONE-1.md` for supported and pending
functionality. The patch library is fetched at runtime and is not redistributed
by this repository.
