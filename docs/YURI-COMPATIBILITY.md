# Yuri compatibility contract

Kirikiri Vita treats Kirikiroid2Yuri's backend and its known-working Android
runtime as the retail compatibility contract. Kirikiri SDL2 is only a source
of individually reviewed portable fixes; it does not supply the engine core.

At engine startup, `TVPLoadInternalPlugins()` collects the ncbind static
registrations. Calls such as `Plugins.link("addFont.dll")` are routed to that
registry, matching Yuri's sealed-plugin behavior. The build also verifies that
every expected module name remains in the linked Vita ELF.

| Yuri module | Vita status | Implementation |
| --- | --- | --- |
| `xp3filter.dll` | Implemented | Yuri decoder and content-filter bridge, with Vita thread-local adaptation |
| `addFont.dll` | Implemented | Yuri `System.addFont` API over the current `FontSystem`; required external MS Gothic face 0 and archive TTF/OTF/TTC use FreeType; the release ELF has no ScePvf fallback |
| `csvParser.dll` | Implemented | Yuri source |
| `dirlist.dll` | Implemented | Yuri API adapted to the current storage-media lister |
| `fstat.dll` | Implemented | `Storages.dirlist` compatibility API used by KAGEX directory helpers |
| `fftgraph.dll` | Implemented | Yuri compatibility function |
| `getSample.dll` | Implemented | Yuri `WaveSoundBuffer` extensions |
| `getabout.dll` | Implemented | Yuri `System.getAboutString` extension |
| `perspective.dll` | Implemented | Yuri `Layer.perspectiveCopy` API, adapted to the current CPU bitmap core with inverse-homography sampling |
| `shrinkCopy.dll` | Implemented | Original Kirikiri-compatible `Layer.shrinkCopy` and `Layer.shrinkCopyFast` area-average APIs, with bounded coefficient storage |
| `saveStruct.dll` | Implemented | Yuri source |
| `varfile.dll` | Implemented | Yuri in-memory storage medium |
| `win32dialog.dll` | Implemented | Yuri's platform-neutral TJS compatibility class |
| `wutcwf.dll` | Implemented | Yuri TCWF audio decoder |
| `extNagano.dll` | Compatibility fallback | Closed-source Nagano transition names (`3duniversal`, `blurfade`, `scanline`, `zoomfade`, `rgbfade`, `spin`, `flutter`, `book`, `imagewipe`, `honeyturn`, `morphing`, `multiripple`) are registered against Yuri's crossfade handler; transition timing and standard options remain honored, but the proprietary pixel effects are not pixel-identical |
| `krflash.dll` | Load-only fallback | Registers the closed-source Windows ActiveX bridge so titles that only link it during startup can continue. Flash playback and `FlashPlayer` rendering remain unsupported and must stay visible in the compatibility gate when exercised |
| `gfxEffect.dll` | Control-flow fallback | Registers the documented `gfxFire` object, methods, and properties so scripts do not fail at link/configuration time. The closed-source fire-pixel kernel is a no-op fallback; visual fire effects are not claimed pixel-identical |
| `motionplayer.dll` | Control-flow fallback | Registers the `Motion.ResourceManager`, `Motion.Player`, and `Motion.SeparateLayerAdaptor` API used by KAGEX startup and motion layers. The fallback preserves control flow and timing but does not implement the proprietary PSB/E-mote renderer; animated motion pixels are not claimed pixel-identical |
| `layerExDraw.dll` | Control-flow fallback | Registers the `Layer.drawImage*` and `GdiPlus.Image` names used by KAGEX affine layers. Layer-to-layer control flow remains callable; proprietary GDI+ image/affine pixels are not claimed pixel-identical |
| `scriptsEx.dll` | Implemented | The upstream wamsoft implementation, vendored in `third_party/scriptsEx` and attached to Kirikiri's built-in `Scripts` class: `getObjectCount`/`getObjectKeys` over `GetCount`/`EnumMembers`, deep `clone`, structural `equalStruct`, `propGet`/`propSet` with the `pf*` flags, `foreach`, and MD5 hashing. It replaced a TJS fallback whose `getObjectCount` always returned `0`, because TJS2 dictionaries have no `count` member |
| `layerExMovie.dll` | Not implemented | Depends on Yuri's old FFmpeg player and GPU texture bridge; VitaSDK supplies modern FFmpeg libraries, but the decoder/player API needs a dedicated port |

Each row's status corresponds to a `krkrvita::YuriPluginFidelity` level in
`include/krkrvita/yuri_plugin_capabilities.hpp`, so the table and the gate
cannot disagree. "Control-flow fallback" means every documented name exists
and returns, and every operation that would produce pixels or audio is a
no-op. A game that reaches one of those operations on its visible path is
blocked even though nothing throws; `yuri_plugin_fidelity_is_placeholder()`
marks exactly those modules.

`tests/test_yuri_plugin_registry.cpp` drives the real sealed registry
(`ncbAutoRegister::AllRegist` then `LoadModule`) and executes each fallback on
Yuri's TJS VM. Compiling a module, or finding its boot-trace marker in the
ELF, proves neither that `Plugins.link` accepts the name nor that the surface
runs.

`krmovie.dll` is accepted as a Yuri core-loader alias so KAG projects which
unconditionally link the Windows codec DLL can initialize. This does not claim
movie playback support: constructing a movie still reaches the explicit Vita
video-overlay unsupported path until that backend is ported.

Retail KAG conductors frequently request `tkdlVerbose`, which logs every
scenario line, completed macro and call-stack change. The Vita parser preserves
the public debug level and all `tkdlSimple` scenario diagnostics, errors and
explicit `Debug` notices, but suppresses those verbose-only records. The Vita
console sink otherwise performs synchronous `ux0` file operations for each
fragment; large macro libraries can delay their first visible scenario by
minutes without this platform policy.

Large `[iscript]` blocks are assembled with an exact two-pass CRLF builder.
This preserves the string passed to KAG's `onScript` callback while replacing
the old repeated `ttstr +=` growth with one bounded backing allocation. The
retail compatibility gate includes the 737-line 湯けむり configuration block,
and Vita emits entered/completed markers around both its assembly and
execution when a block contains at least 128 lines.

TJS's built-in `Dictionary.saveStruct` emits punctuation, keys and values as
many small UTF-16 fragments. Android hides that pattern behind a memory-backed
local stream, whereas Vita intentionally keeps local files seekable and direct
to avoid duplicating a large save image. The Vita text writer therefore
aggregates uncompressed/simple-crypt fragments in a fixed 8 KiB buffer and
flushes before closing the file; zlib mode retains its separate 64 KiB output
window. This preserves exact serialized bytes and synchronous durability while
avoiding thousands of `sceIoWrite` calls per KAG system-variable save. The
host and physical Cortex-A9 gates pin mixed-fragment output parity and the
bounded sink-call count.

## Retail compatibility gate

The immutable local corpus manifest is
`tests/retail_compatibility_manifest.txt`. Each Phase-1 entry is accepted only
after its directory fingerprint and expected inferred filter agree, every XP3
opens through that filter, every TJS/KAG script decodes, all boot-reachable TJS
and KAG inline-script blocks compile with Yuri's own bytecode compiler, and
every PNG/JPEG/BMP/Ogg/WAVE payload is decrypted and signature-checked. Empty
sentinel assets and text payloads using a media-looking extension are counted
separately. Known unsupported movie, Flash and PSB formats are rejected whether
they are archived or loose beside the executable. Literal plug-in requests are
checked against `yuri_plugin_capabilities.hpp`, the same inventory consumed by
the sealed Vita loader. Caught/optional requests are additionally scanned for
the script-visible API surface of their module; a caught DLL load followed by
a required missing global/class is a boot failure, not an optional feature.
Dormant requests remain visible in the report without being confused with an
unconditional boot requirement.
Dormant invalid scenario blocks and unresolved over-approximated storage edges
are likewise reported separately. A Phase-2 entry must instead reproduce the
explicit executable-analysis diagnosis.

`./scripts/run-retail-compatibility.sh --require-all` is the final product
gate. `cmake --build build-host --target krkrvita-final-compatibility-gate`
provides the same fail-closed target. Both run every row and aggregate failures.
They deliberately fail until every manifest entry is compatible and DeepOne
has completed its Phase-2 result; passing an individual decrypt or
compiler test is not a claim that its unsupported runtime features work.
Ordinary CTest treats a `runtime_blocked` row as a pinned negative regression:
the exact diagnostic must still reproduce. Removing that blocker makes the
test fail until the row is deliberately promoted to `phase1`; the final gate
always rejects both `runtime_blocked` and `phase2` rows.

This gate proves the immutable corpus can be decrypted and parsed by the exact
Yuri TJS compiler and that all statically identifiable boot dependencies fall
within declared Vita capabilities. It cannot prove data-dependent interactive
branches or GPU/audio behavior without executing the title. A physical-Vita
smoke run therefore remains the last release check; Vita3K is not part of this
project's evidence chain.

The optional physical Cortex-A9 lane is a CPU/ABI supplement, not a storage
mirror. `./scripts/run-retail-matrix-on-cortex-a9.sh` transfers only a static
ARMv7 test executable and a temporary compact bundle of decoded startup
scripts from titles that currently pass the host capability gate. The wrapper
rejects XP3/EXE/DLL/media/save payloads, caps the flat
bundle at 64 files and 8 MiB, and removes it after execution. Complete game
directories never leave the workstation for this test.

`LayerExBase.cpp` is a helper for obsolete/disabled plugin paths rather than a
loadable module. `KAGParser.dll` and `menu.dll` remain part of the Yuri native
contract. The temporary krkrz bring-up executable contains compatibility
implementations, but the product target must build them against Yuri's own
TJS, window and layer ABI.

Sharin no Kuni's DVD-volume startup guard is supported by Vita's generated
storage backend. `Storages.searchCD(nonEmptyLabel)` resolves to the selected
mounted project path, while an empty label remains a miss; this is the engine
equivalent of the supplied patched Windows executable's skipped disc branch.

Sharin no Kuni is nevertheless **runtime-blocked** on physical Vita as of the
August 2026 retest. It reaches the title and scenario, but graphics using
transparency (including title UI and the message box) render completely black
through the software renderer. Pinning destination-additive-alpha operations
to Yuri's byte-exact scalar TVPGL implementations corrected a measurable ARMv7
rounding difference, but did not change the hardware symptom. That experiment
must not be described as the fix or as evidence that the title is compatible.
The current host compatibility checks prove archive/filter, dependency, ABI,
and selected arithmetic contracts; they do not reproduce this unresolved
physical-Vita visual failure.

## Core native-class parity

The Yuri and Vita global native-class factories are compared separately from
the plugin inventory. The three Yuri globals removed by the newer core are
restored on Vita: `CDDASoundBuffer`, `MIDISoundBuffer`, and `Pad`.

This preserves Yuri's Android behavior. CDDA and MIDI expose their original
TJS classes but are compatibility shells because Yuri disables the Windows
CD/MIDI backends on Android. `Pad` is Kirikiri's legacy script-editor object,
not controller input; Yuri's non-Windows implementation exposes its properties
as no-ops, which is also the Vita behavior.

## Known engine-level gap

Yuri's movie and `layerExMovie.dll` code is coupled to its older FFmpeg and
texture adapters. It remains a runtime gap until it is adapted to VitaSDK's
FFmpeg libraries and the Vita presentation boundary and then tested. It must
not be represented as supported before that point.
