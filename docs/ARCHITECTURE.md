# Architecture

## Compatibility policy

The Yuri engine core and its internal native plugins define compatibility.
Kirikiri SDL2 may be consulted for individual fixes, but it is not an upstream
base. Windows PE plugins are never executed on Vita; supported plugins are
reimplemented as statically registered native modules.

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
- `runtime`: engine lifecycle and Yuri adapter.
- `platform/vita`: VitaGL, OpenAL, controller/touch, networking and app launch.
- `bubble`: Vita `PARAM.SFO`, indexed PNG assets, installer and direct booter.
- `tool`: host-side inspection and deterministic tests using retail metadata.

## System fonts

Retail Kirikiri titles normally name an installed Windows font instead of
shipping its files. On Vita those requests resolve to the firmware Japanese
Gothic font through `libpvf`; the Latin system font is retained as a glyph
fallback. `scePvfGetCharInfo` supplies proportional metrics and
`scePvfGetCharGlyphImage` rasterizes 8-bit coverage. The VitaGL frontend packs
that coverage into a 1024x1024 `GL_ALPHA` texture atlas and lays out UTF-8 text
with PVF kerning. The atlas is runtime-only: system font files or persistent
copies of their glyph data are never placed in the application package.

The Yuri font-class adapter will consume the same PVF rasterization path.
Unknown Windows face names deliberately fall back to the Japanese system font;
game-supplied fonts can be handled separately. The launcher already uses PVF;
the engine adapter remains part of the title-screen milestone.

Direct bubbles contain only a small booter. It passes a stable game ID to the
central `KRVITA001` application with `sceAppMgrLaunchAppByName2`, avoiding a
full engine copy per game.
