# Kirikiri Vita

## Install and play

1. Enable **Unsafe Homebrew** in **Settings → HENkaku Settings**, then install
   the latest `krkrvita-yuri.vpk` with VitaShell. Extended permissions are
   required to access the shader compiler on `ur0:`.
2. Install `libshacccg.suprx` using an installer such as ShaRKBR33D.
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
5. Copy one Kirikiri game directory, unchanged, below `ux0:data/krkrvita/games/`. Keep exactly one game directory there when no `active.ini` profile has been prepared.
6. Launch **Kirikiri Vita** from LiveArea. 

`active.ini` usage example:
```ini
# Kirikiri Vita per-game profile
game_id=Ｇ線上の魔王
game_path=ux0:data/krkrvita/games/Ｇ線上の魔王
display_name=Ｇ線上の魔王
filter_mode=phase1
patch_title=
patch_brand=
patch_commit=archive-only
patch_root=
xp3_filter_path=
filter_origin=phase1-runtime
input_mapping_version=4
analog_deadzone=0.18
cursor_speed=780
touch_enabled=true
bind.circle=key_enter
bind.cross=mouse_right
bind.dpad_down=key_down
bind.dpad_left=key_left
bind.dpad_right=key_right
bind.dpad_up=key_up
bind.front_touch=mouse_absolute
bind.left_stick=mouse_cursor
bind.ltrigger=mouse_left
bind.rtrigger=key_control
bind.select=disabled
bind.square=disabled
bind.start=disabled
bind.triangle=mouse_wheel_up
```

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
