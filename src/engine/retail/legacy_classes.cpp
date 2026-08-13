#include "DebugIntf.h"
#include "KAGParser.h"
#define tTJSSpinLock tTJSCriticalSection
#define tTJSSpinLockHolder tTJSCriticalSectionHolder
#include "MenuItemIntf.h"

namespace {

void register_class(iTJSDispatch2 *global, const tjs_char *name,
					iTJSDispatch2 *instance)
{
	tTJSVariant value(instance);
	instance->Release();
	global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, name, NULL, &value, global);
}

} // namespace

void krkrvita_register_legacy_classes(iTJSDispatch2 *global)
{
	// These are DLLs on desktop Kirikiri but built into Kirikiroid2.  Retail
	// startup scripts still call Plugins.link; the SDL no-op is intentional,
	// while the actual native classes must already exist here.
	register_class(global, TJS_W("KAGParser"), TVPCreateNativeClass_KAGParser());
	register_class(global, TJS_W("MenuItem"), TVPCreateNativeClass_MenuItem());
}

void TVPShowPopMenu(tTJSNI_MenuItem *)
{
	// MenuItem state and callbacks remain functional. A desktop popup menu has
	// no Vita equivalent; game-native menu layers and controller bindings are
	// the primary UI path.
}
