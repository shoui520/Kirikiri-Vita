#include "ncbind/ncbind.hpp"

#include "krkrvita/layerexdraw_surface.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#include <string>

#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

namespace {

void register_layerexdraw_surface() {
    const auto script = TJS::ttstr(
        std::string(krkrvita::layerexdraw_surface_script));
    TVPExecuteScript(script.c_str(), TJS_W("krkrvita-layerexdraw-surface.tjs"), 0);
    krkrvita_boot_trace("retail-layerexdraw-surface-ready");
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_layerexdraw_surface);
