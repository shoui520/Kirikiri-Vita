#include "tjsCommHead.h"

#include "ScriptMgnIntf.h"
#include "TransIntf.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "ncbind/ncbind.hpp"

#include <array>

// extNagano is a closed-source transition plug-in.  Its public contract is
// the set of transition provider names below; the implementation-specific
// pixel kernels are not available to reuse.  Registering those names with
// Yuri's crossfade provider keeps KAGEX scripts runnable and still honours
// the standard time/options path.  This is intentionally a compatibility
// fallback, not a claim that Nagano's visual effects are pixel-identical.
#define NCB_MODULE_NAME TJS_W("extNagano.dll")

namespace {

class ExtNaganoFallbackProvider final
    : public tTVPCrossFadeTransHandlerProvider {
public:
    explicit ExtNaganoFallbackProvider(const tjs_char* name) : name_(name) {}

    tjs_error TJS_INTF_METHOD GetName(const tjs_char** name) override {
        if (!name) return TJS_E_FAIL;
        *name = name_;
        return TJS_S_OK;
    }

private:
    const tjs_char* name_;
};

constexpr std::array<const tjs_char*, 12> kTransitionNames = {
    TJS_W("3duniversal"), TJS_W("blurfade"), TJS_W("scanline"),
    TJS_W("zoomfade"), TJS_W("rgbfade"), TJS_W("spin"),
    TJS_W("flutter"), TJS_W("book"), TJS_W("imagewipe"),
    TJS_W("honeyturn"), TJS_W("morphing"), TJS_W("multiripple")};

std::array<iTVPTransHandlerProvider*, kTransitionNames.size()> providers{};

// KAGEX's environment consults these tables before it asks Yuri for a
// transition provider.  extNagano normally creates them while linking its
// native module; install the same public names/options without replacing an
// existing game-defined table.
void install_transition_tables() {
    TVPExecuteScript(TJS_W(
        "if (typeof global.transitionName == \"undefined\") "
        "global.transitionName = %[];"
        "if (typeof global.transitionParam == \"undefined\") "
        "global.transitionParam = %[];"
        "global.transitionName[\"3duniversal\"] = true;"
        "global.transitionName[\"blurfade\"] = true;"
        "global.transitionName[\"scanline\"] = true;"
        "global.transitionName[\"zoomfade\"] = true;"
        "global.transitionName[\"rgbfade\"] = true;"
        "global.transitionName[\"spin\"] = true;"
        "global.transitionName[\"flutter\"] = true;"
        "global.transitionName[\"book\"] = true;"
        "global.transitionName[\"imagewipe\"] = true;"
        "global.transitionName[\"honeyturn\"] = true;"
        "global.transitionName[\"morphing\"] = true;"
        "global.transitionName[\"multiripple\"] = true;"
        "global.transitionParam[\"time\"] = true;"
        "global.transitionParam[\"rule\"] = true;"
        "global.transitionParam[\"vague\"] = true;"
        "global.transitionParam[\"bound1\"] = true;"
        "global.transitionParam[\"bound2\"] = true;"
        "global.transitionParam[\"speed1\"] = true;"
        "global.transitionParam[\"speed2\"] = true;"
        "global.transitionParam[\"accel1\"] = true;"
        "global.transitionParam[\"accel2\"] = true;"
        "global.transitionParam[\"type\"] = true;"
        "global.transitionParam[\"type1\"] = true;"
        "global.transitionParam[\"type2\"] = true;"
        "global.transitionParam[\"exponent\"] = true;"
        "global.transitionParam[\"prerender\"] = true;"
        "global.transitionParam[\"blur1\"] = true;"
        "global.transitionParam[\"blur2\"] = true;"
        "global.transitionParam[\"blur1x\"] = true;"
        "global.transitionParam[\"blur1y\"] = true;"
        "global.transitionParam[\"blur2x\"] = true;"
        "global.transitionParam[\"blur2y\"] = true;"
        "global.transitionParam[\"alpha\"] = true;"
        "global.transitionParam[\"slip\"] = true;"
        "global.transitionParam[\"back\"] = true;"
        "global.transitionParam[\"order\"] = true;"
        "global.transitionParam[\"twist\"] = true;"
        "global.transitionParam[\"size\"] = true;"
        "global.transitionParam[\"count\"] = true;"
        "global.transitionParam[\"after\"] = true;"
        "global.transitionParam[\"before\"] = true;"
        "global.transitionParam[\"callback\"] = true;"
        "global.transitionParam[\"delaylast\"] = true;"
        "global.transitionParam[\"roundness\"] = true;"
        "global.transitionParam[\"maxdrift\"] = true;"
        "global.transitionParam[\"rwidth\"] = true;"
        "global.transitionParam[\"wavecount\"] = true;"
        "global.transitionParam[\"delayA\"] = true;"
        "global.transitionParam[\"delayB\"] = true;"
        "global.transitionParam[\"delayG\"] = true;"
        "global.transitionParam[\"delayR\"] = true;"
        "global.transitionParam[\"zoom1\"] = true;"
        "global.transitionParam[\"zoom2\"] = true;"),
        TJS_W("extNagano-compat.tjs"), 0);
}

void register_extnagano() {
    install_transition_tables();
    for (std::size_t i = 0; i < kTransitionNames.size(); ++i) {
        auto* provider = new ExtNaganoFallbackProvider(kTransitionNames[i]);
        TVPAddTransHandlerProvider(provider);
        provider->Release();
        providers[i] = provider;
    }
    krkrvita_boot_trace("retail-extnagano-crossfade-fallback-ready");
}

void unregister_extnagano() {
    for (auto*& provider : providers) {
        if (!provider) continue;
        TVPRemoveTransHandlerProvider(provider);
        provider = nullptr;
    }
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_extnagano);
NCB_POST_UNREGIST_CALLBACK(unregister_extnagano);
