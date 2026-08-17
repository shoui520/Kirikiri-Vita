#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

// The layerExBTOA surface itself comes from the upstream wamsoft
// implementation vendored in third_party/layerExBTOA (see its README). That
// module attaches the real Layer members, so this translation unit only
// records that the plug-in reached registration for the hardware boot log.
#define NCB_MODULE_NAME TJS_W("layerExBTOA.dll")

namespace {

void trace_layerexbtoa_ready() {
    krkrvita_boot_trace("retail-layerexbtoa-surface-ready");
}

} // namespace

NCB_POST_REGIST_CALLBACK(trace_layerexbtoa_ready);
