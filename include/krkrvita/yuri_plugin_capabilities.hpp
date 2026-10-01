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
    X("fbfsteamplugin.dll")                         \
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
    X("layerexbtoa.dll")                            \
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
        "fbfsteamplugin.dll", {"fbfsteam.", "cfbfsteam", "", ""}},
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
    // layerExBTOA attaches to Layer rather than exposing a namespace of its
    // own, so its script-visible family is the method names themselves.
    YuriPluginSurfaceContract{
        "layerexbtoa.dll", {"copyrightbluetoleftalpha", "copybottombluetotopalpha",
                            "copyalphatoprovince", "fillbyprovince"}},
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
    return module == "fbfsteamplugin.dll" ||
           module == "motionplayer.dll" || module == "gfxeffect.dll" ||
           module == "layerexdraw.dll" || module == "scriptsex.dll";
}

// How closely the Vita build reproduces a plug-in.  A single "supported"
// boolean has repeatedly been read as "this game works": a module can be
// linkable, expose every documented name, and still draw nothing.  Report the
// level instead so a reachable no-op is describable as the gap it is.
enum class YuriPluginFidelity {
    // The sealed registry does not know the module; Plugins.link throws.
    unsupported,
    // Links so a caught load succeeds, but supplies no behaviour at all.
    link_only,
    // Supplies the script-visible names and keeps KAG control flow alive,
    // while the operations that produce pixels or audio are no-ops.
    control_flow_fallback,
    // Real behaviour with documented gaps, or a deliberate substitution such
    // as mapping a closed-source transition set onto a crossfade.
    behavioral_subset,
    // The upstream or an equivalent portable implementation is compiled in.
    portable_equivalent,
};

constexpr YuriPluginFidelity yuri_plugin_fidelity(std::string_view module) {
    if (!yuri_plugin_link_is_supported(module))
        return YuriPluginFidelity::unsupported;
    // Offline support: real locale-based language selection, callable Steam
    // lifecycle/achievement methods, but no Steam connection or stat uploads.
    if (module == "fbfsteamplugin.dll")
        return YuriPluginFidelity::behavioral_subset;
    // krflash never plays Flash; the link exists only so startup continues.
    if (module == "krflash.dll") return YuriPluginFidelity::link_only;
    // gfxEffect keeps gfxFire's state and call surface; the fire kernel is a
    // no-op.  motionplayer and layerExDraw expose their classes but draw
    // nothing: no E-mote/PSB renderer and no GDI+ backing exist on Vita.
    if (module == "gfxeffect.dll" || module == "motionplayer.dll" ||
        module == "layerexdraw.dll")
        return YuriPluginFidelity::control_flow_fallback;
    // extNagano's documented provider names are mapped onto Yuri's crossfade,
    // so transitions run with the right timing but not the right pixels.
    if (module == "extnagano.dll") return YuriPluginFidelity::behavioral_subset;
    return YuriPluginFidelity::portable_equivalent;
}

// A reachable call into one of these modules is a compatibility blocker even
// when startup no longer throws, because the game gets a plausible answer
// instead of the effect it asked for.
constexpr bool yuri_plugin_fidelity_is_placeholder(std::string_view module) {
    const auto fidelity = yuri_plugin_fidelity(module);
    return fidelity == YuriPluginFidelity::link_only ||
           fidelity == YuriPluginFidelity::control_flow_fallback;
}

} // namespace krkrvita
