#pragma once

#include <string_view>

namespace krkrvita {

// Some KAGEX titles catch motionplayer.dll load failures but
// still instantiate the Motion namespace from the same script unit.  Keep the
// fallback in one literal so the Vita registration and the host compatibility
// test execute exactly the same TJS source.  This is the script/API surface;
// it is deliberately not presented as a pixel-identical E-mote renderer.
inline constexpr std::string_view motionplayer_surface_script = R"TJS(
if (typeof global.Motion == "undefined") global.Motion = %[];

class KrkrVitaMotionResourceManager {
    var cache;
    var cacheSize;
    function KrkrVitaMotionResourceManager(kag, size) {
        cache = %[];
        cacheSize = size;
    }
    function load(path) { cache[path] = true; return void; }
    function unload(path) { delete cache[path]; return void; }
    function clearCache() { cache = %[]; return void; }
    function findSource(path) { return void; }
    function requireLayerId(path) { return 0; }
    function releaseLayerId(path) { return void; }
}

class KrkrVitaMotionPlayer {
    var resourceManager;
    var playing = false;
    var allplaying = false;
    var motion = "";
    var chara = "";
    var stealthChara = false;
    var stealthMotion = false;
    var tickCount = 0;
    var lastTime = 0;
    var speed = 1.0;
    var completionType = 0;
    var angleDeg = 0;
    var tags = [];
    var coordX = 0;
    var coordY = 0;
    var flipX = false;
    var flipY = false;
    var slantX = 0;
    var slantY = 0;
    var zoomX = 1.0;
    var zoomY = 1.0;
    var scaleX = 1.0;
    var scaleY = 1.0;
    var onAction = void;

    function KrkrVitaMotionPlayer(manager) {
        resourceManager = manager;
        tags = [];
    }
    function play(name, flags) {
        motion = name;
        tickCount = 0;
        lastTime = 0;
        allplaying = flags !== void && +flags != 0;
        playing = (name != "normal" && name != "status");
        return void;
    }
    function stop() { playing = false; allplaying = false; return void; }
    function skipToSync() { playing = false; allplaying = false; return void; }
    function progress(interval) {
        if (interval !== void) {
            tickCount += +interval;
            lastTime = tickCount;
        }
        if (playing && tickCount >= 100) {
            playing = false;
            allplaying = false;
        }
        return void;
    }
    function draw(target) { return void; }
    function clear(target, color) { return void; }
    function setCoord(x, y) { coordX = x; coordY = y; return void; }
    function setFlip(x, y) { flipX = x; flipY = y; return void; }
    function setSlant(x, y) { slantX = x; slantY = y; return void; }
    function setZoom(x, y) { zoomX = x; zoomY = y; return void; }
    function setScale(x, y) { scaleX = x; scaleY = y; return void; }
    function setRot(value) { angleDeg = value; return void; }
    function getRot() { return angleDeg; }
    function setDrawAffineTranslateMatrix(a, b, c, d, e, f) { return void; }
    function contains(x, y) { return false; }
    function setVariable(name, value) { this[name] = value; return void; }
    function getVariable(name) { return this[name]; }
    function getCommandList() { return []; }
    function getLayerMotion(name) { return this; }
    function getLayerGetter(name) { return []; }
}

class KrkrVitaSeparateLayerAdaptor {
    var targetLayer;
    var absolute = false;
    var face = 0;
    var imageWidth = 0;
    var imageHeight = 0;
    function KrkrVitaSeparateLayerAdaptor(parent) { targetLayer = parent; }
    function assign(source) {
        targetLayer = source.targetLayer;
        absolute = source.absolute;
    }
    function loadImages(storage) { return void; }
    function fillRect(left, top, width, height, color) { return void; }
    function operateRect(left, top, width, height, mode, opa, type, sx, sy) { return void; }
    function clear() { return void; }
}

global.Motion.ResourceManager = KrkrVitaMotionResourceManager;
global.Motion.Player = KrkrVitaMotionPlayer;
global.Motion.SeparateLayerAdaptor = KrkrVitaSeparateLayerAdaptor;
global.Motion.PlayFlagForce = 1;
global.Motion.PlayFlagChain = 2;
global.Motion.PlayFlagAsCan = 4;
global.Motion.PlayFlagJoin = 8;
global.Motion.PlayFlagStealth = 16;
global.Motion.Player.useD3D = 0;
global.Motion.Player.enableD3D = %[];
)TJS";

} // namespace krkrvita
