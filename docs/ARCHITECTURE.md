# Architecture

## Compatibility policy

Kirikiroid2Yuri is the engine backend: TJS2, native classes, KAG-facing
window/layer objects, storage, timers, persistence, image codecs, the sound
graph and internal plugins form one ABI-coupled unit. They are ported together,
not overlaid piecemeal onto krkrz. The known-working Yuri Android `libgame.so`
is the behavioral and exported-symbol oracle for the milestone retail game.

Kirikiri SDL2/krkrz may contribute reviewed portable fixes, but it is not the
runtime base and its rejection of retail extraction filters is not inherited.
Windows PE plugins are never executed on Vita; required Yuri plugins are
statically registered native modules.

## Port boundary

The Android/Cocos user interface is outside the port. The game selector,
preferences, menus, Android lifecycle and Cocos scene graph are not backend
dependencies and do not belong in the Vita engine target.

The Vita boundary implements only the services the Yuri core consumes:

- filesystem/storage and platform paths;
- time, threads, events and application lifetime;
- an `iWindowLayer` implementation for controller, touch and window events;
- Yuri's stable software compositor, with its completed CPU framebuffer
  uploaded by the narrow VitaGL presentation layer. Static software textures
  use Yuri's low-overhead lossless `lz4` store, while allocation pressure delivers the
  engine's maximum compact event and drains deferred textures before retry;
- required external MS Gothic face 0 rasterization through Yuri's FreeType
  backend;
- a sound-device sink for Yuri's mixer; and
- network/cache access for the patch repository.

Any Cocos types leaking through Yuri headers are adapter debt to make opaque
at this boundary. They are not grounds for porting the Cocos frontend.

## Startup pipeline

1. Scan the game directory and produce a stable fingerprint.
2. Resolve a patch bundle from a cached, pinned Kirikiroid2 patch manifest.
3. Ask the user only when multiple candidates have comparable confidence.
4. Fall back to a game-local filter, then archive-only heuristic detection.
5. Register `xp3filter` before opening protected archive contents.
6. Apply the rest of the selected patch bundle at its declared startup phase.
7. Start the engine using the game's persistent profile.

`GameStorage` now implements the common namespace used by startup: loose patch
files override loose game files, loose files override archives, patch-numbered
XP3 archives have descending priority, lookups are Windows-style
case-insensitive, and `Storages.addAutoPath` prefixes are retained. Filtered XP3
bytes are decoded before the Kirikiri UTF-8/UTF-16/FE-FE text codec sees them.

Downloaded script is executable content. The resolver pins the remote commit,
rejects unsafe paths and executable file types, records SHA-256 digests, and
keeps an offline cache.

## Vita components

- `compat`: game scanning, PE resources, patch matching and filter heuristics.
- `engine`: the wholesale Yuri backend plus its statically registered retail
  modules.
- `platform/vita`: software-framebuffer VitaGL presentation, sound-device output,
  controller/touch, FreeType integration, networking and app launch.
- `bubble`: Vita `PARAM.SFO`, indexed PNG assets, installer and direct booter.
- `tool`: host-side inspection and deterministic tests using retail metadata.

The default build does not compile or register Yuri's experimental OpenGL
compositor. VitaGL owns only the final 960x544 presentation path and cursor;
Yuri's software renderer remains the authority for layer and transition
semantics. The OpenGL compositor is retained behind an explicit build option
for later work.

## System fonts

Retail Kirikiri titles normally name an installed Windows font instead of
shipping its files. The primary Vita configuration reads the user's external
`ux0:data/krkrvita/msgothic.ttc`, verifies TTC face 0, and selects its
non-proportional `ＭＳ ゴシック` family as the default. Yuri's FreeType path
then owns the original requested pixel height, hinting, metrics and glyph
placement; none of the PVF raster growth or baseline corrections apply.
The TTC's UI Gothic and PGothic faces remain distinct collection entries and
are never substituted for the default MS Gothic face. The external font is not
packaged or redistributed. It is a required personal-use runtime file: a
missing or invalid face 0 is reported explicitly, and the Yuri release ELF does
not link the ScePvf rasterizer or SDK stubs.

Direct bubbles contain only a small booter. It passes a validated stable game
ID to the central `KRVITA001` application with `sceAppMgrLaunchAppByName2`.
The engine selects `ux0:data/krkrvita/profiles/<game-id>.ini`, avoiding a full
engine copy per game. Bubble icons are decoded from the retail PE resources and
written as indexed Vita PNGs; the SFO title and ID are generated per game.

## Retail patch layout

The deploy profile points to a private patch directory rather than modifying
the retail game. Loose compatibility scripts in that directory are added to
Kirikiri's auto path. Any `*.xp3` files in the same bundle are mounted as
archive auto paths in descending filename order before `patch.tjs` executes.
`xp3filter.tjs` is installed still earlier, before protected archive content is
opened.
