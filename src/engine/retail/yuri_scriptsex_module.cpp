#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

// The scriptsEx surface itself comes from the upstream wamsoft implementation
// fetched from the pinned KrKr2-Next source. That module registers
// the real Scripts members, so this translation unit only records that the
// plug-in reached registration for the hardware boot log.
#define NCB_MODULE_NAME TJS_W("scriptsEx.dll")

namespace {

void trace_scriptsex_ready() {
    krkrvita_boot_trace("retail-scriptsex-surface-ready");
}

} // namespace

NCB_POST_REGIST_CALLBACK(trace_scriptsex_ready);
