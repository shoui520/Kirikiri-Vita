# layerExBTOA (vendored)

`layerExBTOA.cpp` is the portable ncbind implementation of the wamsoft
`layerExBTOA.dll` plug-in, taken from the KrKr2-Next Kirikiri 2 port.

| | |
| --- | --- |
| Upstream plug-in | https://github.com/wamsoft/layerExBTOA |
| Imported from | `KrKr2-Next` `cpp/plugins/layerExBTOA.cpp` |
| Source revision | `1abd1ed4e8aec7abd5d2524c3a9ad7886880602f` (2026-04-21) |
| Original SHA-256 | `6a9395f18af263941956186e99b42db8af18eb8275b04a8b044aa8f543e31fa7` |

## Local modifications

* `#include "ncbind.hpp"` was changed to `#include "ncbind/ncbind.hpp"` to
  match Yuri's include layout. No other change.

## What it provides

Eight methods attached to Kirikiri's built-in `Layer` class, all operating
directly on the layer's main and province image buffers:

* `copyRightBlueToLeftAlpha` / `copyBottomBlueToTopAlpha` — the plug-in's
  namesake. A single image holds colour on one half and a greyscale mask on the
  other; these fold the mask's blue channel into the colour half's alpha and
  shrink the layer to the colour half. It is how a Kirikiri game ships an
  alpha-blended sprite in a format with no alpha channel.
* `fillAlpha`, `clipAlphaRect`, `overwrapRect` — alpha-plane fills and clips.
* `copyAlphaToProvince`, `fillByProvince`, `fillToProvince` — province-plane
  transfers, used for hit testing against a mask.

## Why it is vendored rather than stubbed

The plug-in is linked unguarded — `Plugins.link('plugin/layerExBTOA.dll')` with
no `try`/`catch` — by titles that use it, so an absent module ends the boot
before the first scene. A link-only stub would get past that line and then
produce sprites with no transparency, which is the "callable but wrong" outcome
the compatibility gate exists to reject. This file computes the real pixels.

Covered by `tests/test_yuri_plugin_registry.cpp`.
