#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "MenuItemImpl.h"

void TVPShowPopMenu(tTJSNI_MenuItem*) {
    // Popup construction belongs to the future Vita frontend. Keep the engine
    // menu object valid, but report that this platform edge cannot present it.
    TVPAddImportantLog(TJS_W("(warning) popup menu presentation is unavailable"));
}
