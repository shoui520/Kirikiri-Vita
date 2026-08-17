# Milestone 1: 色情教団

The milestone is complete when the untouched retail directory can be copied to
the Vita and all of the following work:

- identify the game and select the ORCSOFT/DWARFSOFT patch bundle;
- fetch and cache `patch.tjs` and `xp3filter.tjs`;
- register the hash-XOR extraction filter before XP3 content is read;
- replace the Windows `xp3dec.tpm` path with the internal filter plugin;
- reach the real title screen and scenario with correct text and rendering;
- select externally supplied `msgothic.ttc` face 0 (`ＭＳ ゴシック`) through
  Yuri's unadjusted FreeType path as a required personal-use runtime file;
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
- execution of the Yuri TJS filter VM across `data.xp3`, `events.xp3` and
  `voice.xp3`, with all 48 distributed samples recognized after decryption;
- inventory and decryption of all 64 TJS scripts and all 198 KAG scenarios,
  followed by compilation of every TJS unit with Yuri's compiler;
- full pixel decoding of the first game background and three title-screen PNGs,
  plus FFmpeg decoding of encrypted BGM and voice data;
- pinning of the real `startup.tjs`/`Boot.tjs`/`first.ks`/`SysTitle.ks` route,
  all six requested native DLL contracts and all seven extrans methods;
- a complete Vita cross-build of the Yuri backend with its stable software
  compositor feeding a VitaGL screen presenter, Yuri's lossless compressed
  static-texture store and allocation-pressure cache reclamation, OpenAL output,
  controller/touch profiles, and mandatory external MS Gothic face-0 selection
  through Yuri FreeType, with no ScePvf code in the release ELF;
- a strong-whole-archive pthread contract, corrected auto-reset event semantics,
  and an on-device thread/mutex/condition-variable/join self-test before Yuri;
- generation of `active.ini` and `profiles/44bb539bf9510882.ini`; and
- extraction and Vita PNG conversion of the Windows executable icon. Direct
  VPK generation is disabled after its metadata failed hardware validation.

Still requiring a real Vita run before this milestone can be called complete:

- boot through the real title screen and scenario;
- verify MS Gothic glyph metrics and line layout on firmware;
- verify BGM, effects, voices, saves and load paths;
- tune controller/touch behavior if the title assumes Windows-specific input.

## Reproducible hardware-test payload

```sh
VITASDK=/home/shoui/vitasdk JOBS=2 ./scripts/build-vita.sh
./build-host/krkrvita-tool vita-stage \
  '/home/shoui/Agents/CodexMax/色情教団' \
  .cache/patches .cache/vita-stage \
  'ux0:data/krkrvita/games/色情教団'
```

Install `build-vita-yuri/krkrvita-yuri.vpk`, copy the untouched game directory to
`ux0:data/krkrvita/games/色情教団`, and copy the contents of
`.cache/vita-stage` into `ux0:data/krkrvita`. Supply the personal system-font
copy separately as `ux0:data/krkrvita/msgothic.ttc`; it is intentionally not
embedded in the VPK. Direct bubbles are currently disabled pending safe
hardware validation of their SFO and uninstall path.

Upstream VitaGL requires `ur0:/data/libshacccg.suprx`; install it legally with
ShaRKBR33D or VitaDB Downloader. Engine build `01.11` checks this before VitaGL
startup. It also recreates `ux0:data/krkrvita/boot-status.txt` from a
pre-constructor hook and uses raw Vita I/O for `error.txt`, so a launch that
returns to LiveArea can be assigned to the loader, constructors, engine, or
renderer without depending on the normal Kirikiri logger.

After leaving the title running for 30 seconds, validate the copied trace with
`./scripts/verify-vita-run.sh /path/to/boot-status.txt`. This rejects partial
starts and requires synchronized worker threads, retail patch/filter selection,
`extrans` and `layerExSave`, completed TJS startup, a presented game frame,
started audio, and 30 seconds of continued event-loop execution.

Build `01.10` restores Kirikiroid/Yuri's pointer-valued `Window.HWND`
contract. Yuri's native `Window.menu` implementation uses that value to own
and cache a complete `MenuItem` tree; SDL omitted the property on Vita, which
made KAG's first `menu.add(...)` call operate on an invalid value.

Build `01.11` presents the uploaded game framebuffer with VitaGL client arrays.
The previous immediate-mode quad path had initialized VitaGL with a zero-byte
immediate vertex pool, so its first `glVertex2f` dereferenced a null pool.
