#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

// krflash.dll is the Windows ActiveX Flash bridge.  There is no Flash ActiveX
// runtime on Vita, and the original binary is closed-source.  Some titles
// nevertheless link it unconditionally while their selected route never
// creates a FlashPlayer instance.  Register the module so those titles keep
// booting; a title that actually asks for Flash playback must remain an
// explicit compatibility failure rather than silently claiming a renderer.
#define NCB_MODULE_NAME TJS_W("krflash.dll")

namespace {

void mark_krflash_load_only() {
    krkrvita_boot_trace("retail-krflash-load-only-ready");
}

} // namespace

NCB_POST_REGIST_CALLBACK(mark_krflash_load_only);
