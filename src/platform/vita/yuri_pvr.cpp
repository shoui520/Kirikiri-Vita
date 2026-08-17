#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "RenderManager.h"
#include "krkrvita/retail_bootstrap.hpp"

#include <atomic>
#include <functional>

namespace {

void throw_pvr_unavailable() {
    // PVRv3 is Kirikiroid's mobile texture-cache format, not a Windows retail
    // Kirikiri storage format.  Do not silently decode it incorrectly when no
    // PVRTC decoder is linked into the Vita backend.
    TVPThrowExceptionMessage(
        TVPImageLoadError,
        TJS_W("PVRv3 mobile texture cache is not supported on Vita"));
}

} // namespace

void TVPLoadPVRv3(void*, void*, tTVPGraphicSizeCallback,
                  tTVPGraphicScanLineCallback, tTVPMetaInfoPushCallback,
                  tTJSBinaryStream*, tjs_int, tTVPGraphicLoadMode) {
    throw_pvr_unavailable();
}

void TVPLoadHeaderPVRv3(void*, tTJSBinaryStream*, iTJSDispatch2**) {
    throw_pvr_unavailable();
}

iTVPTexture2D* TVPLoadPVRv3(
    tTJSBinaryStream*,
    const std::function<void(const ttstr&, const tTJSVariant&)>&) {
    // GraphicsLoaderIntf calls this direct-texture hook for every image, not
    // only for PVR files. Yuri's software RenderManager returns nullptr here
    // to say "not handled", after which the normal PNG/JPEG/TLG loader runs.
    // Throwing from this hook therefore misclassified ordinary retail PNGs as
    // unsupported PVR caches and prevented the bitmap fallback entirely.
    static std::atomic_flag traced = ATOMIC_FLAG_INIT;
    if (!traced.test_and_set(std::memory_order_relaxed))
        krkrvita_boot_trace("yuri-direct-texture-fallback");
    return nullptr;
}
