# Milestone 1: 色情教団

The milestone is complete when the untouched retail directory can be copied to
the Vita and all of the following work:

- identify the game and select the ORCSOFT/DWARFSOFT patch bundle;
- fetch and cache `patch.tjs` and `xp3filter.tjs`;
- register the hash-XOR extraction filter before XP3 content is read;
- replace the Windows `xp3dec.tpm` path with the internal filter plugin;
- reach the real title screen and scenario with correct text and rendering;
- resolve absent Windows fonts to Vita's Japanese system font through PVF;
- play BGM, sound effects and voices;
- accept controller and front-touch input through a per-game profile;
- save and load persistent data; and
- generate and install a direct LiveArea bubble using the EXE icon and title.

Passing a launcher smoke test is not considered game compatibility.

## Current verification boundary

Completed off-device for the milestone game:

- exact automatic match to `ORCSOFT／DWARFSOFT/色情教団` at the pinned patch
  commit;
- byte-identical staging of `patch.tjs` and `xp3filter.tjs`;
- execution of the Yuri TJS filter VM against `data.xp3`, with all 32 sampled
  blocks recognized after decryption;
- a complete Vita cross-build of the current Kirikiri SDL2/krkrz runtime with
  the Yuri extraction-filter ABI, VitaGL presentation, SDL Vita audio,
  controller/touch profiles and ScePvf font rasterization;
- generation of `active.ini` and `profiles/44bb539bf9510882.ini`; and
- extraction and Vita PNG conversion of the Windows executable icon. Direct
  VPK generation is disabled after its metadata failed hardware validation.

Still requiring a real Vita run before this milestone can be called complete:

- boot through the real title screen and scenario;
- verify PVF Japanese glyph metrics and line layout on firmware;
- verify BGM, effects, voices, saves and load paths;
- tune controller/touch behavior if the title assumes Windows-specific input.

## Reproducible hardware-test payload

```sh
VITASDK=/home/shoui/vitasdk JOBS=4 ./scripts/build-vita.sh
./build-host/krkrvita-tool vita-stage \
  '/home/shoui/Agents/CodexMax/色情教団' \
  .cache/patches .cache/vita-stage \
  'ux0:data/krkrvita/games/色情教団'
```

Install `build-vita-engine/krkrsdl2.vpk`, copy the untouched game directory to
`ux0:data/krkrvita/games/色情教団`, and copy the contents of
`.cache/vita-stage` into `ux0:data/krkrvita`. Direct bubbles are currently
disabled pending safe hardware validation of their SFO and uninstall path.

Upstream VitaGL requires `ur0:/data/libshacccg.suprx`; install it legally with
ShaRKBR33D or VitaDB Downloader. Engine build `01.08` checks this before VitaGL
startup. It also recreates `ux0:data/krkrvita/boot-status.txt` from a
pre-constructor hook and uses raw Vita I/O for `error.txt`, so a launch that
returns to LiveArea can be assigned to the loader, constructors, engine, or
renderer without depending on the normal Kirikiri logger.
