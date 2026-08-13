# Architecture

## Compatibility policy

The current Kirikiri SDL2/krkrz engine is imported wholesale: TJS2, native
classes, KAG-facing window/layer objects, storage, timers, persistence, image
codecs and the sound stack. Yuri supplies the retail `xp3filter` native plugin
semantics that current Kirikiri SDL2 intentionally omits. Windows PE plugins
are never executed on Vita; required plugins are statically registered native
modules.

## Startup pipeline

1. Scan the game directory and produce a stable fingerprint.
2. Resolve a patch bundle from a cached, pinned Kirikiroid2 patch manifest.
3. Ask the user only when multiple candidates have comparable confidence.
4. Fall back to heuristic extraction-filter detection, then game-local files.
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
- `engine`: the wholesale core plus narrowly scoped retail and Vita adapters.
- `platform/vita`: VitaGL presentation, SDL Vita audio, controller/touch,
  networking and app launch.
- `bubble`: Vita `PARAM.SFO`, indexed PNG assets, installer and direct booter.
- `tool`: host-side inspection and deterministic tests using retail metadata.

## System fonts

Retail Kirikiri titles normally name an installed Windows font instead of
shipping its files. On Vita the engine's `FontRasterizer` resolves those names
to the shared firmware Japanese font through ScePvf, retaining the shared Latin
font as a glyph fallback. `scePvfGetCharInfo` supplies proportional metrics and
`scePvfGetCharGlyphImage` rasterizes 8-bit coverage directly into Kirikiri
character bitmaps. Unknown Windows face names deliberately fall back to the
Japanese system font. Explicit game-added font files still use the upstream
FreeType rasterizer. Firmware font data is never copied into the VPK.

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
