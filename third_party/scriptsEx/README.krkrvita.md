# scriptsEx (vendored)

`scriptsEx.cpp` is the portable ncbind implementation of the wamsoft
`scriptsEx.dll` plug-in, taken from the KrKr2-Next Kirikiri 2 port.

| | |
| --- | --- |
| Upstream plug-in | https://github.com/wamsoft/scriptsEx |
| Imported from | `KrKr2-Next` `cpp/plugins/scriptsEx.cpp` |
| Source revision | `1abd1ed4e8aec7abd5d2524c3a9ad7886880602f` (2026-04-21) |
| Original SHA-256 | `8871e031dbc834adc55b14d5e9c26c9be8e83b1598c22a9568ecb2d3580e4707` |

## Local modifications

* `#include "ncbind.hpp"` was changed to `#include "ncbind/ncbind.hpp"` to
  match Yuri's include layout. No other change.

## Why it is vendored rather than reimplemented

The Vita backend previously registered a hand-written TJS fallback for
`scriptsEx.dll`. That fallback was not equivalent: it answered
`Scripts.getObjectCount()` by reading a `count` member, and TJS2 dictionaries
have no such member, so the KAGEX startup path that calls it always received
`0`. `Scripts.clone()` returned its argument, and `Scripts.equalStruct()`
compared references. Those are exactly the "callable but wrong" placeholders the
compatibility gate is supposed to reject.

This file provides the real behaviour — `GetCount`/`EnumMembers`-based counting
and key listing, deep `clone`, structural `equalStruct`, `propGet`/`propSet`
with the documented `pf*` flags, `foreach`, and MD5 hashing — and attaches to
Kirikiri's built-in `Scripts` class, as upstream does.

`Scripts.safeEvalStorage` and `Scripts.loadDataPack` remain as upstream wrote
them; `loadDataPack` returns an empty dictionary upstream too.

Covered by `tests/test_yuri_plugin_registry.cpp`.
