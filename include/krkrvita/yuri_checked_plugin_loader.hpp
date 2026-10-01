#pragma once

#include "MsgIntf.h"
#include "StorageIntf.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/yuri_plugin_capabilities.hpp"

#include <set>

extern bool TVPLoadInternalPlugin(const ttstr& name);
extern std::set<ttstr> TVPRegisteredPlugins;

namespace krkrvita {

// Shared by Vita's Plugins.link implementation and the host execution probe.
// Registration is lazy and a second link must preserve existing instances.
inline bool try_load_yuri_plugin(const ttstr& name) {
    const ttstr module = TVPExtractStorageName(name).AsLowerCase();
    if (TVPRegisteredPlugins.find(module) != TVPRegisteredPlugins.end())
        return true;
    if (TVPLoadInternalPlugin(module)) return true;
    static const tjs_char* const integrated[] = {
#define KRKRVITA_YURI_TJS_PLUGIN(name) TJS_W(name),
        KRKRVITA_YURI_INTEGRATED_PLUGIN_MODULES(KRKRVITA_YURI_TJS_PLUGIN)
#undef KRKRVITA_YURI_TJS_PLUGIN
    };
    for (const auto* candidate : integrated) {
        if (module != candidate) continue;
        TVPRegisteredPlugins.insert(module);
        if (module == TJS_W("krmovie.dll"))
            krkrvita_boot_trace("retail-krmovie-core-alias-ready");
        return true;
    }
    return false;
}

inline void load_yuri_plugin(const ttstr& name) {
    if (!try_load_yuri_plugin(name))
        TVPThrowExceptionMessage(TVPCannotLoadPlugin, name);
}

} // namespace krkrvita
