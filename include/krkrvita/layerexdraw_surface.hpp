#pragma once

#include <string_view>

namespace krkrvita {

// layerExDraw is a Windows/GDI+ plug-in.  KAGEX scripts use its Layer and
// GdiPlus.Image names even when the plug-in load is wrapped in a catch.  This
// fallback keeps those names callable on Vita; ordinary Layer-to-Layer copies
// are delegated to Yuri's native methods and image/affine operations that
// require the proprietary GDI+ backing are deliberately no-op fallbacks.
inline constexpr std::string_view layerexdraw_surface_script = R"TJS(
if (typeof global.GdiPlus == "undefined") global.GdiPlus = %[];
if (typeof global.Layer == "undefined") global.Layer = %[];

class KrkrVitaGdiPlusImage {
    var imageLeft = 0;
    var imageTop = 0;
    var imageWidth = 0;
    var imageHeight = 0;
    var width = 0;
    var height = 0;
    var _storage = "";

    function KrkrVitaGdiPlusImage() {}
    function load(storage) { _storage = storage; return void; }
    function Clone() {
        // Unqualified inside a method, this name resolves to the constructor
        // member on "this" rather than the class object, so it must be
        // reached through global.
        var result = new global.KrkrVitaGdiPlusImage();
        result.imageLeft = imageLeft;
        result.imageTop = imageTop;
        result.imageWidth = imageWidth;
        result.imageHeight = imageHeight;
        result.width = width;
        result.height = height;
        result._storage = _storage;
        return result;
    }
    function GetBounds() {
        return %[x:imageLeft, y:imageTop, width:imageWidth, height:imageHeight];
    }
    function GetWidth() { return imageWidth; }
    function GetHeight() { return imageHeight; }
    function GetHorizontalResolution() { return 96; }
    function GetVerticalResolution() { return 96; }
    function setSize(w, h) {
        imageWidth = +w; imageHeight = +h; width = +w; height = +h;
        return void;
    }
    function setSizeToImageSize() { width = imageWidth; height = imageHeight; return void; }
    function assignImages(src) {
        imageLeft = src.imageLeft; imageTop = src.imageTop;
        imageWidth = src.imageWidth; imageHeight = src.imageHeight;
        width = src.width; height = src.height; return void;
    }
    function clear() { return void; }
    function adjustGamma() { return void; }
    function affineBlend() { return void; }
    function affineCopy() { return void; }
    function affinePile() { return void; }
    function blendRect() { return void; }
    function colorRect() { return void; }
    function copyRect() { return void; }
    function doBoxBlur() { return void; }
    function doGrayScale() { return void; }
    function drawText() { return void; }
    function fillRect() { return void; }
    function flipLR() { return void; }
    function flipUD() { return void; }
    function independMainImage() { return void; }
    function independProvinceImage() { return void; }
    function operateAffine() { return void; }
    function operateRect() { return void; }
    function operateStretch() { return void; }
    function pileRect() { return void; }
    function shrinkCopy() { return void; }
    function clipAlphaRect() { return void; }
    function updateAffine() { return void; }
    function drawAffine() { return void; }
}

global.GdiPlus.Image = KrkrVitaGdiPlusImage;

function krkrvitaLayerExDrawRect(left, top, width, height) {
    return [left, top, left + width - 1, top + height - 1];
}

if (typeof global.Layer.drawImage == "undefined") {
    global.Layer.drawImage = function(left, top, source) {
        var width = 0;
        var height = 0;
        if (source.imageWidth !== void) width = source.imageWidth;
        if (source.imageHeight !== void) height = source.imageHeight;
        return krkrvitaLayerExDrawRect(left, top, width|0, height|0);
    };
}
if (typeof global.Layer.drawImageRect == "undefined") {
    global.Layer.drawImageRect = function(left, top, source, sl, st, sw, sh) {
        return krkrvitaLayerExDrawRect(left, top, sw|0, sh|0);
    };
}
if (typeof global.Layer.drawImageStretch == "undefined") {
    global.Layer.drawImageStretch = function(left, top, width, height, source, sl, st, sw, sh) {
        return krkrvitaLayerExDrawRect(left, top, width|0, height|0);
    };
}
if (typeof global.Layer.drawImageAffine == "undefined") {
    global.Layer.drawImageAffine = function(source, sl, st, sw, sh, affine, A, B, C, D, E, F) {
        var left = affine ? E : A;
        var top = affine ? F : B;
        return krkrvitaLayerExDrawRect(left, top, sw|0, sh|0);
    };
}
)TJS";

} // namespace krkrvita
