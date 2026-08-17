#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/scriptsex_surface.hpp"

#include <string>

#define NCB_MODULE_NAME TJS_W("scriptsEx.dll")

namespace {

void register_scriptsex_surface() {
    const auto script = TJS::ttstr(
        std::string(krkrvita::scriptsex_surface_script));
    TVPExecuteScript(script.c_str(), TJS_W("krkrvita-scriptsex-surface.tjs"), 0);
    krkrvita_boot_trace("retail-scriptsex-surface-ready");
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_scriptsex_surface);
