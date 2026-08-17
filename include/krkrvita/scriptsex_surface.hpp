#pragma once

#include <string_view>

namespace krkrvita {

// Small script-compatible subset of scriptsEx.  KAGEX uses getObjectCount
// during startup; the remaining helpers are conservative fallbacks that keep
// optional utility paths callable without pretending to implement the native
// SQ/thread/file extensions.
inline constexpr std::string_view scriptsex_surface_script = R"TJS(
function krkrvitaScriptsExCount(value) {
    if (value === void || value === null) return 0;
    if (value.count !== void) return +value.count;
    return 0;
}

if (typeof global.Scripts == "undefined") global.Scripts = %[];
if (typeof global.Scripts.getObjectCount == "undefined")
    global.Scripts.getObjectCount = function(value) { return krkrvitaScriptsExCount(value); };
if (typeof global.Scripts.getObjectKeys == "undefined")
    global.Scripts.getObjectKeys = function(value) {
        return [];
    };
if (typeof global.Scripts.getObjectContext == "undefined")
    global.Scripts.getObjectContext = function(value) { return value; };
if (typeof global.Scripts.isNullContext == "undefined")
    global.Scripts.isNullContext = function(value) { return value === null || value === void; };
if (typeof global.Scripts.equalStruct == "undefined")
    global.Scripts.equalStruct = function(left, right) { return left == right; };
if (typeof global.Scripts.equalStructNumericLoose == "undefined")
    global.Scripts.equalStructNumericLoose = function(left, right) { return left == right; };
if (typeof global.Scripts.clone == "undefined")
    global.Scripts.clone = function(value) { return value; };
if (typeof global.Scripts.propSet == "undefined")
    global.Scripts.propSet = function(object, name, value) { object[name] = value; return value; };
if (typeof global.Scripts.propGet == "undefined")
    global.Scripts.propGet = function(object, name) { return object[name]; };
)TJS";

} // namespace krkrvita
