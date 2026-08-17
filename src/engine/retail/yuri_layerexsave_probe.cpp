#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

#define NCB_MODULE_NAME TJS_W("layerExSave.dll")

namespace {

void mark_layerexsave_ready() {
    // A post-registration callback proves that all synchronous Layer methods
    // from the module were attached before Plugins.link returned to the game.
    krkrvita_boot_trace("retail-layerexsave-ready");
}

} // namespace

NCB_POST_REGIST_CALLBACK(mark_layerexsave_ready);
