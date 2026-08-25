#include "DebugIntf.h"
#include "KAGParser.h"
#include "MsgIntf.h"
#define tTJSSpinLock tTJSCriticalSection
#define tTJSSpinLockHolder tTJSCriticalSectionHolder
#include "WindowIntf.h"
#include "MenuItemIntf.h"

namespace {

class WindowNativeHandleProperty final : public tTJSDispatch
{
public:
	tjs_error TJS_INTF_METHOD PropGet(tjs_uint32, const tjs_char *,
									tjs_uint32 *, tTJSVariant *result,
									iTJSDispatch2 *objthis) override
	{
		if(!objthis) return TJS_E_INVALIDOBJECT;

		iTJSNativeInstance *instance = nullptr;
		const tjs_error status = objthis->NativeInstanceSupport(
			TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, &instance);
		if(TJS_FAILED(status) || !instance) return TJS_E_INVALIDOBJECT;

		// Kirikiroid/Yuri deliberately exposes the native Window instance here,
		// not an operating-system window handle. MenuItem uses it as a stable
		// owner identity and compares it with TVPGetWindowListAt().
		if(result)
			*result = static_cast<tTVInteger>(reinterpret_cast<tjs_intptr_t>(instance));
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD PropSet(tjs_uint32, const tjs_char *,
									tjs_uint32 *, const tTJSVariant *,
									iTJSDispatch2 *) override
	{
		return TJS_E_ACCESSDENYED;
	}
};

void require_member(iTJSDispatch2 *object, const tjs_char *member,
					const tjs_char *failure)
{
	tTJSVariant value;
	if(!object || TJS_FAILED(object->PropGet(TJS_IGNOREPROP, member, nullptr,
											&value, object)))
		TVPThrowExceptionMessage(failure);
}

iTJSDispatch2 *get_class(iTJSDispatch2 *global, const tjs_char *name,
						 const tjs_char *failure, tTJSVariant &holder)
{
	if(TJS_FAILED(global->PropGet(TJS_IGNOREPROP, name, nullptr, &holder, global)))
		TVPThrowExceptionMessage(failure);
	iTJSDispatch2 *object = holder.AsObjectNoAddRef();
	if(!object) TVPThrowExceptionMessage(failure);
	return object;
}

void install_yuri_window_compatibility(iTJSDispatch2 *global)
{
	tTJSVariant window_holder;
	iTJSDispatch2 *window_class = get_class(global, TJS_W("Window"),
		TJS_W("Kirikiri Vita: Window class is unavailable"), window_holder);

	auto *property = new WindowNativeHandleProperty();
	tTJSVariant value(property);
	property->Release();
	if(TJS_FAILED(window_class->PropSet(TJS_MEMBERENSURE, TJS_W("HWND"),
									 nullptr, &value, window_class)))
		TVPThrowExceptionMessage(
			TJS_W("Kirikiri Vita: cannot install Yuri Window.HWND compatibility"));

	require_member(window_class, TJS_W("HWND"),
		TJS_W("Kirikiri Vita: Yuri Window.HWND compatibility is missing"));
}

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

	// Yuri's MenuItem implementation gets the root menu through Window.HWND.
	// The SDL implementation omits that property outside Windows, so port
	// Yuri's pointer-valued ABI before MenuItem installs Window.menu.
	install_yuri_window_compatibility(global);
	tTJSNativeClass *menu_item = TVPCreateNativeClass_MenuItem();
	require_member(menu_item, TJS_W("add"),
		TJS_W("Kirikiri Vita: MenuItem.add compatibility is missing"));
	register_class(global, TJS_W("MenuItem"), menu_item);

	tTJSVariant window_holder;
	iTJSDispatch2 *window_class = get_class(global, TJS_W("Window"),
		TJS_W("Kirikiri Vita: Window class is unavailable"), window_holder);
	require_member(window_class, TJS_W("menu"),
		TJS_W("Kirikiri Vita: Window.menu compatibility is missing"));
}

void TVPShowPopMenu(tTJSNI_MenuItem *)
{
	// MenuItem state and callbacks remain functional. A desktop popup menu has
	// no Vita equivalent; game-native menu layers and controller bindings are
	// the primary UI path.
}
