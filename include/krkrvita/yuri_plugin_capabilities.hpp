#pragma once

#include <array>
#include <string_view>

// Keep the host compatibility auditor and the Vita sealed loader on one
// module inventory.  "Integrated" modules are supplied by Yuri's core;
// "internal" modules are real statically registered implementations linked
// into krkrvita-yuri-plugins.  A successful link is never treated as proof
// that an unsupported Windows DLL exists.
#define KRKRVITA_YURI_INTEGRATED_PLUGIN_MODULES(X) \
    X("menu.dll")                                      \
    X("kagparser.dll")                                 \
    X("wuvorbis.dll")                                  \
    X("krmovie.dll")                                   \
    X("motionplayer.dll")                              \
    X("squirrel.dll")

#define KRKRVITA_YURI_INTERNAL_PLUGIN_MODULES(X) \
    X("addfont.dll")                                \
    X("csvparser.dll")                              \
    X("dirlist.dll")                                \
    X("fftgraph.dll")                               \
    X("fstat.dll")                                  \
    X("getabout.dll")                               \
    X("getsample.dll")                              \
    X("perspective.dll")                            \
    X("savestruct.dll")                             \
    X("varfile.dll")                                \
    X("win32dialog.dll")                            \
    X("wutcwf.dll")                                 \
    X("xp3filter.dll")                              \
    X("extrans.dll")                                \
    X("extnagano.dll")                              \
    X("krflash.dll")                                \
    X("gfxeffect.dll")                              \
    X("layerexdraw.dll")                            \
    X("scriptsex.dll")                              \
    X("layerexsave.dll")                            \
    X("layereximage.dll")                           \
    X("shrinkcopy.dll")                             \
    X("sigcheck.dll")                               \
    X("psd.dll")                                    \
    X("psbfile.dll")                               \
    X("sqlite3.dll")

namespace krkrvita {

#define KRKRVITA_YURI_PLUGIN_STRING(name) std::string_view{name},
inline constexpr auto yuri_integrated_plugin_modules = std::array{
    KRKRVITA_YURI_INTEGRATED_PLUGIN_MODULES(KRKRVITA_YURI_PLUGIN_STRING)
};
inline constexpr auto yuri_internal_plugin_modules = std::array{
    KRKRVITA_YURI_INTERNAL_PLUGIN_MODULES(KRKRVITA_YURI_PLUGIN_STRING)
};
#undef KRKRVITA_YURI_PLUGIN_STRING

constexpr bool yuri_plugin_link_is_supported(std::string_view module) {
    for (const auto candidate : yuri_integrated_plugin_modules)
        if (module == candidate) return true;
    for (const auto candidate : yuri_internal_plugin_modules)
        if (module == candidate) return true;
    return false;
}

// A module may be linkable while its runtime feature is intentionally absent.
// Keep this predicate for future partial ports; krmovie is no longer one of
// them because the Vita build contains the FFmpeg decode/presentation backend.
constexpr bool yuri_plugin_is_link_only(std::string_view module) {
    return module == "krflash.dll" || module == "gfxeffect.dll" ||
           module == "extnagano.dll";
}

// A caught Plugins.link is not evidence that the script can continue: many
// KAGEX games use the plug-in's globals immediately after the catch.  These
// markers are intentionally conservative and describe script-visible API
// families, not implementation details.  The host gate uses them to reject
// an optional DLL when its required global/class is referenced in the same
// source unit.
struct YuriPluginSurfaceContract {
    std::string_view module;
    std::array<std::string_view, 4> markers;
};

inline constexpr auto yuri_plugin_surface_contracts = std::array{
    YuriPluginSurfaceContract{
        "motionplayer.dll", {"motion.", "motionaffinesourcelayer", "motion_", ""}},
    YuriPluginSurfaceContract{
        "emoteplayer.dll", {"emote.", "emoteplayer", "emote_", ""}},
    YuriPluginSurfaceContract{
        "krflash.dll", {"flashplayer", "flash.", "flash_", ""}},
    YuriPluginSurfaceContract{
        "gfxeffect.dll", {"gfxfire", "gfxeffect", "gfx_", ""}},
    YuriPluginSurfaceContract{
        "layerexraster.dll", {"raster.", "layerexraster", "raster_", ""}},
    YuriPluginSurfaceContract{
        "layerexdraw.dll", {"layer.drawimage", "drawimageaffine", "drawimagestretch", "gdiplus.image"}},
    YuriPluginSurfaceContract{
        "layerexbtoa.dll", {"layerexbtoa.", "layerexbtoa_", "btoa.", ""}},
    YuriPluginSurfaceContract{
        "layerexsubimage.dll", {"subimage.", "layerexsubimage_", "subimage_", ""}},
    YuriPluginSurfaceContract{
        "windowex.dll", {"windowex.", "windowex_", "window_ex", ""}},
    YuriPluginSurfaceContract{
        "scriptsex.dll", {"scripts.getobjectcount", "scripts.getobjectkeys", "scripts.getobjectcontext", "scripts.getthreadcountsq"}},
};

constexpr const YuriPluginSurfaceContract* yuri_plugin_surface_contract(
    std::string_view module) {
    for (const auto& contract : yuri_plugin_surface_contracts)
        if (contract.module == module) return &contract;
    return nullptr;
}

// True means the Vita build supplies the script-visible object/class surface
// even when the proprietary pixel/audio implementation is a fallback.  A
// load-only module such as krflash intentionally returns false.
constexpr bool yuri_plugin_has_script_surface(std::string_view module) {
    return module == "motionplayer.dll" || module == "gfxeffect.dll" ||
           module == "layerexdraw.dll" || module == "scriptsex.dll";
}

} // namespace krkrvita
