#include "ncbind/ncbind.hpp"

#include "krkrvita/motionplayer_surface.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#include <string>

// Several KAGEX titles link motionplayer.dll inside a catch
// block, then immediately use Motion.ResourceManager/Player.  A missing DLL
// therefore looks optional to a literal plugin scanner but is a hard startup
// dependency in practice.  This Vita surface preserves the script contract
// and keeps ordinary KAG control flow alive while the full PSB/Motion renderer
// remains a separate implementation project.  It deliberately does not claim
// pixel-identical E-mote rendering.
#define NCB_MODULE_NAME TJS_W("motionplayer.dll")

namespace {

void register_motionplayer_surface() {
    const auto script = TJS::ttstr(
        std::string(krkrvita::motionplayer_surface_script));
    TVPExecuteScript(script.c_str(), TJS_W("krkrvita-motionplayer-surface.tjs"), 0);
    krkrvita_boot_trace("retail-motionplayer-surface-ready");
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_motionplayer_surface);
