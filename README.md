# Kirikiri Vita

## Install and play

1. Install the latest `krkrvita-yuri.vpk` with VitaShell.
2. Install `libshacccg.suprx` at `ur0:/data/libshacccg.suprx` using a legal
   installer such as ShaRKBR33D or VitaDB Downloader.
3. Create this directory layout on the Vita memory card:

   ```text
   ux0:data/krkrvita/
   ├── msgothic.ttc
   └── games/
       └── My Game/
           ├── data.xp3
           ├── startup.tjs
           └── ...the rest of the original game files
   ```

4. Supply your own `msgothic.ttc` at exactly
   `ux0:data/krkrvita/msgothic.ttc`. The font is required and is not included
   in the VPK.
5. Copy one legally owned Kirikiri game directory, unchanged, below
   `ux0:data/krkrvita/games/`. Keep exactly one game directory there when no
   `active.ini` profile has been prepared.
6. Launch **Kirikiri Vita** from LiveArea. The application selects the game,
   applies a matching embedded compatibility patch when available, and starts
   it.

## Controls

- Left stick: move the mouse pointer
- Front touch screen: position the mouse pointer
- L: left click
- Cross: right click
- Circle: Enter
- Triangle: mouse-wheel up
- R: hold Control (skip)
- D-pad: arrow keys

If startup fails, read `ux0:data/krkrvita/error.txt`. Progress diagnostics are
written to `ux0:data/krkrvita/boot-status.txt`.
