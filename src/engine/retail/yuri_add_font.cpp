#include "ncbind/ncbind.hpp"

#include "FontSystem.h"
#include "StorageIntf.h"

#include <vector>

#define NCB_MODULE_NAME TJS_W("addFont.dll")

extern FontSystem *TVPFontSystem;

namespace {

struct YuriSystemFontExtension
{
	static tjs_error TJS_INTF_METHOD AddFont(tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *)
	{
		if(numparams < 1) return TJS_E_BADPARAMCOUNT;

		const ttstr placed = TVPGetPlacedPath(*param[0]);
		if(placed.IsEmpty() || !TVPFontSystem)
		{
			if(result) *result = 0;
			return TJS_S_OK;
		}

		// The Vita rasterizer delegates explicit TTF/OTF data to FreeType and
		// retains scePvf as the fallback for games which rely on system fonts.
		std::vector<ttstr> faces;
		TVPFontSystem->AddExtraFont(placed.AsStdString(), &faces);
		if(result) *result = static_cast<tjs_int>(faces.size());
		return TJS_S_OK;
	}
};

} // namespace

NCB_ATTACH_CLASS(YuriSystemFontExtension, System)
{
	RawCallback(TJS_W("addFont"), &YuriSystemFontExtension::AddFont,
		TJS_STATICMEMBER);
}
