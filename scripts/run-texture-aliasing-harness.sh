#!/usr/bin/env bash
# Build and run the host texture/compositing harness.
#
# Why this exists: a retail black-layer defect consumed several
# flash-install-run-collect cycles on the user's physical Vita, and each cycle
# could only eliminate one hypothesis. Yuri's bitmap, texture and render-method
# code compiles and runs natively, so those hypotheses can be tested in seconds
# here instead.
#
# Scope: the bitmap/texture/render layer **and** the compositor itself --
# LayerIntf.cpp (tTJSNI_BaseLayer) and LayerManager.cpp (tTVPLayerManager) both
# compile and link natively, and tTJSNI_BaseLayer is directly instantiable
# without the TJS object system. That means a layer tree can be built, filled,
# drawn and inspected here.
#
# This links the **generated** RenderManager.cpp, LayerBitmapIntf.cpp,
# LayerIntf.cpp and LayerManager.cpp from the Vita build tree, not the upstream
# originals, so the code under test is the product's own -- including the patched iTVPTexture2D::IsIndependent() that
# trades copy-on-write safety for a skipped copy when the presenter holds a
# reference. Testing the upstream sources instead would silently exercise stock
# semantics and pass regardless.
#
# Requirements:
#   - a configured Vita build tree (build-vita-yuri) for the generated sources
#   - a built host tree (build-host) for libkrkrvita-yuri-tjs.a and oniguruma
#   - ffmpeg headers, which the Vita ffmpeg cache already provides
#
# tests/yuri_texture_harness_stubs.cpp supplies the surrounding engine. Every
# stub there aborts rather than returning a plausible value, so if the code
# under test starts depending on one the harness fails loudly instead of
# quietly testing a fiction.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
cd "$source_root"

vita_generated=${KRKRVITA_VITA_GENERATED:-build-vita-yuri/generated/yuri}
host_build=${KRKRVITA_HOST_BUILD:-build-host}
ffmpeg_include=${KRKRVITA_FFMPEG_INCLUDE:-.cache/ffmpeg-vita-8.1.1/include}
freetype_include=${KRKRVITA_FREETYPE_INCLUDE:-/usr/include/freetype2}
out=${KRKRVITA_HARNESS_OUT:-$(mktemp -d)/krkrvita-texture-harness}
# Which reproduction to build. tests/test_yuri_texture_aliasing.cpp covers the
# texture lifetime; tests/test_yuri_layer_composite.cpp drives a real layer tree
# through tTVPLayerManager.
harness_test=${KRKRVITA_HARNESS_TEST:-tests/test_yuri_texture_aliasing.cpp}

for required in \
    "$vita_generated/RenderManager.cpp" \
    "$vita_generated/RenderManager.h" \
    "$vita_generated/LayerBitmapIntf.cpp" \
    "$vita_generated/LayerIntf.cpp" \
    "$host_build/libkrkrvita-yuri-tjs.a" \
    "$host_build/_deps/krkrvita_oniguruma-build/libonig.a" \
    "$ffmpeg_include/libswscale/swscale.h"
do
    if [ ! -e "$required" ]; then
        echo "missing prerequisite: $required" >&2
        echo "configure the Vita build and build the host tree first" >&2
        exit 2
    fi
done

core=$host_build/_deps/krkrvita_yuri-src/src/core
includes=(
    -I"$vita_generated"
    -Iinclude
    -I"$ffmpeg_include"
    -I"$freetype_include"
    -I"$core"
    -I"$core/base" -I"$core/base/win32"
    -I"$core/environ" -I"$core/environ/win32" -I"$core/environ/ConfigManager"
    -I"$core/msg" -I"$core/msg/win32"
    -I"$core/tjs2"
    -I"$core/utils" -I"$core/utils/win32"
    -I"$core/visual" -I"$core/visual/win32"
    -Isrc/yuri/compat
    -Isrc/platform/vita
)

g++ -std=gnu++17 -O2 -DNDEBUG -include src/yuri/tjs_compat.hpp "${includes[@]}" \
    "$harness_test" \
    tests/yuri_texture_harness_stubs.cpp \
    tests/yuri_layer_harness_stubs.cpp \
    "$vita_generated/RenderManager.cpp" \
    "$vita_generated/LayerBitmapIntf.cpp" \
    "$vita_generated/LayerIntf.cpp" \
    "$vita_generated/LayerManager.cpp" \
    "$core/msg/MsgIntf.cpp" \
    "$core/visual/TransIntf.cpp" \
    "$core/visual/tvpgl.cpp" \
    "$core/visual/ComplexRect.cpp" \
    "$core/visual/argb.cpp" \
    "$core/visual/CharacterData.cpp" \
    "$core/visual/FontSystem.cpp" \
    "$core/visual/PrerenderedFont.cpp" \
    "$core/visual/FreeType.cpp" \
    "$core/visual/FreeTypeFontRasterizer.cpp" \
    "$core/visual/gl/blend_function.cpp" \
    "$core/visual/win32/BitmapBitsAlloc.cpp" \
    "$core/visual/win32/BitmapInfomation.cpp" \
    "$core/visual/win32/LayerBitmapImpl.cpp" \
    "$host_build/libkrkrvita-yuri-tjs.a" \
    "$host_build/_deps/krkrvita_oniguruma-build/libonig.a" \
    -lfreetype -lpthread \
    -o "$out"

echo "built host texture harness: $out"
exec "$out"
