#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

#include <cstdint>

// gfxEffect.dll is the small, closed-source Kaede Software effect plug-in.
// Its documented public object is gfxFire.  Keep that object and its complete
// state/method surface available on Vita so scripts can configure and advance
// an effect without a missing-member exception.  The proprietary fire pixel
// kernel is not available here, so the fallback deliberately leaves the
// target layer unchanged; compatibility tests must still distinguish this
// from pixel-identical gfxEffect playback.
#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")

namespace {

class GfxFireFallback {
public:
    GfxFireFallback() = default;

    static tjs_error TJS_INTF_METHOD init(
        tTJSVariant*, tjs_int count, tTJSVariant** args, GfxFireFallback* self) {
        if (count < 2) return TJS_E_BADPARAMCOUNT;
        self->target_layer_ = *args[0];
        self->num_seeds_ = static_cast<tjs_int>(args[1]->AsInteger());
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD cycle(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD updateWarpMap(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD updateCoolMap(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD applyBlue(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD applyRed(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD applyWhite(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }
    static tjs_error TJS_INTF_METHOD applyCustom(
        tTJSVariant*, tjs_int, tTJSVariant**, GfxFireFallback*) { return TJS_S_OK; }

    static tjs_error TJS_INTF_METHOD clearFireSeed(
        tTJSVariant*, tjs_int count, tTJSVariant**, GfxFireFallback*) {
        return count < 1 ? TJS_E_BADPARAMCOUNT : TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD setFireSeed(
        tTJSVariant*, tjs_int count, tTJSVariant** args, GfxFireFallback* self) {
        if (count < 2) return TJS_E_BADPARAMCOUNT;
        self->seed_layer_ = *args[0];
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD setFireSeedPos(
        tTJSVariant*, tjs_int count, tTJSVariant** args, GfxFireFallback* self) {
        if (count < 3) return TJS_E_BADPARAMCOUNT;
        self->seed_x_ = static_cast<tjs_int>(args[0]->AsInteger());
        self->seed_y_ = static_cast<tjs_int>(args[1]->AsInteger());
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD setCustomColorTable(
        tTJSVariant*, tjs_int count, tTJSVariant** args, GfxFireFallback* self) {
        if (count < 1) return TJS_E_BADPARAMCOUNT;
        self->custom_color_table_ = *args[0];
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD get_numOfSeeds(
        tTJSVariant* result, tjs_int, tTJSVariant**, GfxFireFallback* self) {
        if (result) *result = self->num_seeds_;
        return TJS_S_OK;
    }

#define GFX_VARIANT_PROPERTY(name, field)                                      \
    static tjs_error TJS_INTF_METHOD get_##name(                               \
        tTJSVariant* result, tjs_int, tTJSVariant**, GfxFireFallback* self) {   \
        if (result) *result = self->field;                                     \
        return TJS_S_OK;                                                       \
    }                                                                           \
    static tjs_error TJS_INTF_METHOD set_##name(                               \
        tTJSVariant*, tjs_int count, tTJSVariant** args, GfxFireFallback* self) { \
        if (count < 1) return TJS_E_BADPARAMCOUNT;                             \
        self->field = *args[0];                                                \
        return TJS_S_OK;                                                       \
    }

    GFX_VARIANT_PROPERTY(seedLayer, seed_layer_)
    GFX_VARIANT_PROPERTY(targetLayer, target_layer_)
    GFX_VARIANT_PROPERTY(seedX, seed_x_)
    GFX_VARIANT_PROPERTY(seedY, seed_y_)
    GFX_VARIANT_PROPERTY(randomSeed, random_seed_)
    GFX_VARIANT_PROPERTY(forceH, force_h_)
    GFX_VARIANT_PROPERTY(forceV, force_v_)
    GFX_VARIANT_PROPERTY(boundRangeH, bound_range_h_)
    GFX_VARIANT_PROPERTY(boundRangeV, bound_range_v_)
    GFX_VARIANT_PROPERTY(scalingCoeff, scaling_coeff_)
    GFX_VARIANT_PROPERTY(numOfBlurForCoolMap, blur_count_)
    GFX_VARIANT_PROPERTY(textureFilterType, texture_filter_type_)
    GFX_VARIANT_PROPERTY(coolRange, cool_range_)
    GFX_VARIANT_PROPERTY(coolStrength, cool_strength_)
    GFX_VARIANT_PROPERTY(coolParticleDensityDenominator, particle_denominator_)
    GFX_VARIANT_PROPERTY(coolParticleDensityNumerator, particle_numerator_)
    GFX_VARIANT_PROPERTY(edgeSmoothing, edge_smoothing_)

#undef GFX_VARIANT_PROPERTY

private:
    tTJSVariant seed_layer_;
    tTJSVariant target_layer_;
    tTJSVariant custom_color_table_;
    tTJSVariant seed_x_;
    tTJSVariant seed_y_;
    tTJSVariant random_seed_;
    tTJSVariant force_h_;
    tTJSVariant force_v_;
    tTJSVariant bound_range_h_;
    tTJSVariant bound_range_v_;
    tTJSVariant scaling_coeff_;
    tTJSVariant blur_count_;
    tTJSVariant texture_filter_type_;
    tTJSVariant cool_range_;
    tTJSVariant cool_strength_;
    tTJSVariant particle_denominator_;
    tTJSVariant particle_numerator_;
    tTJSVariant edge_smoothing_;
    tjs_int num_seeds_ = 0;
};

} // namespace

NCB_REGISTER_CLASS_DIFFER(gfxFire, GfxFireFallback) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD_RAW_CALLBACK(init, &GfxFireFallback::init, 0);
    NCB_METHOD_RAW_CALLBACK(cycle, &GfxFireFallback::cycle, 0);
    NCB_METHOD_RAW_CALLBACK(updateWarpMap, &GfxFireFallback::updateWarpMap, 0);
    NCB_METHOD_RAW_CALLBACK(updateCoolMap, &GfxFireFallback::updateCoolMap, 0);
    NCB_METHOD_RAW_CALLBACK(applyBlue, &GfxFireFallback::applyBlue, 0);
    NCB_METHOD_RAW_CALLBACK(applyRed, &GfxFireFallback::applyRed, 0);
    NCB_METHOD_RAW_CALLBACK(applyWhite, &GfxFireFallback::applyWhite, 0);
    NCB_METHOD_RAW_CALLBACK(applyCustom, &GfxFireFallback::applyCustom, 0);
    NCB_METHOD_RAW_CALLBACK(clearFireSeed, &GfxFireFallback::clearFireSeed, 0);
    NCB_METHOD_RAW_CALLBACK(setFireSeed, &GfxFireFallback::setFireSeed, 0);
    NCB_METHOD_RAW_CALLBACK(setFireSeedPos, &GfxFireFallback::setFireSeedPos, 0);
    NCB_METHOD_RAW_CALLBACK(setCustomColorTable, &GfxFireFallback::setCustomColorTable, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(numOfSeeds, &GfxFireFallback::get_numOfSeeds, 0);
    NCB_PROPERTY_RAW_CALLBACK(seedLayer, &GfxFireFallback::get_seedLayer, &GfxFireFallback::set_seedLayer, 0);
    NCB_PROPERTY_RAW_CALLBACK(targetLayer, &GfxFireFallback::get_targetLayer, &GfxFireFallback::set_targetLayer, 0);
    NCB_PROPERTY_RAW_CALLBACK(seedX, &GfxFireFallback::get_seedX, &GfxFireFallback::set_seedX, 0);
    NCB_PROPERTY_RAW_CALLBACK(seedY, &GfxFireFallback::get_seedY, &GfxFireFallback::set_seedY, 0);
    NCB_PROPERTY_RAW_CALLBACK(randomSeed, &GfxFireFallback::get_randomSeed, &GfxFireFallback::set_randomSeed, 0);
    NCB_PROPERTY_RAW_CALLBACK(forceH, &GfxFireFallback::get_forceH, &GfxFireFallback::set_forceH, 0);
    NCB_PROPERTY_RAW_CALLBACK(forceV, &GfxFireFallback::get_forceV, &GfxFireFallback::set_forceV, 0);
    NCB_PROPERTY_RAW_CALLBACK(boundRangeH, &GfxFireFallback::get_boundRangeH, &GfxFireFallback::set_boundRangeH, 0);
    NCB_PROPERTY_RAW_CALLBACK(boundRangeV, &GfxFireFallback::get_boundRangeV, &GfxFireFallback::set_boundRangeV, 0);
    NCB_PROPERTY_RAW_CALLBACK(scalingCoeff, &GfxFireFallback::get_scalingCoeff, &GfxFireFallback::set_scalingCoeff, 0);
    NCB_PROPERTY_RAW_CALLBACK(numOfBlurForCoolMap, &GfxFireFallback::get_numOfBlurForCoolMap, &GfxFireFallback::set_numOfBlurForCoolMap, 0);
    NCB_PROPERTY_RAW_CALLBACK(textureFilterType, &GfxFireFallback::get_textureFilterType, &GfxFireFallback::set_textureFilterType, 0);
    NCB_PROPERTY_RAW_CALLBACK(coolRange, &GfxFireFallback::get_coolRange, &GfxFireFallback::set_coolRange, 0);
    NCB_PROPERTY_RAW_CALLBACK(coolStrength, &GfxFireFallback::get_coolStrength, &GfxFireFallback::set_coolStrength, 0);
    NCB_PROPERTY_RAW_CALLBACK(coolParticleDensityDenominator, &GfxFireFallback::get_coolParticleDensityDenominator, &GfxFireFallback::set_coolParticleDensityDenominator, 0);
    NCB_PROPERTY_RAW_CALLBACK(coolParticleDensityNumerator, &GfxFireFallback::get_coolParticleDensityNumerator, &GfxFireFallback::set_coolParticleDensityNumerator, 0);
    NCB_PROPERTY_RAW_CALLBACK(edgeSmoothing, &GfxFireFallback::get_edgeSmoothing, &GfxFireFallback::set_edgeSmoothing, 0);
}

namespace {

void mark_gfxeffect_ready() {
    krkrvita_boot_trace("retail-gfxeffect-fallback-ready");
}

} // namespace

NCB_POST_REGIST_CALLBACK(mark_gfxeffect_ready);
