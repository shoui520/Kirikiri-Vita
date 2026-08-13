# Yuri compatibility contract

Kirikiri Vita treats Kirikiroid2Yuri's Android runtime as the retail
compatibility contract. Kirikiri SDL2 supplies the newer cross-platform core;
it is not assumed to contain Yuri-specific native APIs.

At engine startup, `TVPLoadInternalPlugins()` collects the ncbind static
registrations. Calls such as `Plugins.link("addFont.dll")` are routed to that
registry, matching Yuri's sealed-plugin behavior. The build also verifies that
every expected module name remains in the linked Vita ELF.

| Yuri module | Vita status | Implementation |
| --- | --- | --- |
| `xp3filter.dll` | Implemented | Yuri decoder and content-filter bridge, with Vita thread-local adaptation |
| `addFont.dll` | Implemented | Yuri `System.addFont` API over the current `FontSystem`; archive TTF/OTF uses FreeType, system fallback uses ScePvf |
| `csvParser.dll` | Implemented | Yuri source |
| `dirlist.dll` | Implemented | Yuri API adapted to the current storage-media lister |
| `fftgraph.dll` | Implemented | Yuri compatibility function |
| `getSample.dll` | Implemented | Yuri `WaveSoundBuffer` extensions |
| `getabout.dll` | Implemented | Yuri `System.getAboutString` extension |
| `perspective.dll` | Implemented | Yuri `Layer.perspectiveCopy` API, adapted to the current CPU bitmap core with inverse-homography sampling |
| `saveStruct.dll` | Implemented | Yuri source |
| `varfile.dll` | Implemented | Yuri in-memory storage medium |
| `win32dialog.dll` | Implemented | Yuri's platform-neutral TJS compatibility class |
| `wutcwf.dll` | Implemented | Yuri TCWF audio decoder |
| `layerExMovie.dll` | Not implemented | Depends on Yuri's old FFmpeg player and GPU texture bridge; VitaSDK supplies modern FFmpeg libraries, but the decoder/player API needs a dedicated port |

`LayerExBase.cpp` is a helper for obsolete/disabled plugin paths rather than a
loadable module. `KAGParser.dll` and `menu.dll` compatibility are provided by
the dedicated native overlays already compiled into the Vita runtime.

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

Kirikiri SDL2's `VideoOverlay` implementation performs decoding only on
Windows. On Vita its public TJS class exists, but playback methods do not open
or decode media. Therefore video playback remains a known runtime gap even for
games which do not use `layerExMovie.dll`; it must not be represented as
supported until the FFmpeg/VitaGL player is integrated and tested.
