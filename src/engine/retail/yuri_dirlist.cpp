#include "PluginStub.h"
#include "ncbind/ncbind.hpp"

#include <vector>

#define NCB_MODULE_NAME TJS_W("dirlist.dll")

extern void TVPGetStorageListAt(const ttstr &name, std::vector<ttstr> &list);

namespace {

class GetDirListFunction final : public tTJSDispatch
{
	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *membername,
		tjs_uint32 *, tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *) override
	{
		if(membername) return TJS_E_MEMBERNOTFOUND;
		if(numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr directory(*param[0]);
		if(directory.GetLastChar() != TJS_W('/'))
			TVPThrowExceptionMessage(
				TJS_W("'/' must be specified at the end of given directory name."));

		if(!result) return TJS_S_OK;
		std::vector<ttstr> files;
		TVPGetStorageListAt(directory, files);

		iTJSDispatch2 *array = TJSCreateArrayObject();
		try
		{
			tTJSArrayNI *native = nullptr;
			array->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
				TJSGetArrayClassID(), reinterpret_cast<iTJSNativeInstance **>(&native));
			native->Items.insert(native->Items.end(), files.begin(), files.end());
			*result = tTJSVariant(array, array);
			array->Release();
		}
		catch(...)
		{
			array->Release();
			throw;
		}
		return TJS_S_OK;
	}
};

void RegisterDirList()
{
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	GetDirListFunction *function = new GetDirListFunction();
	tTJSVariant value(function);
	function->Release();
	global->PropSet(TJS_MEMBERENSURE, TJS_W("getDirList"), nullptr,
		&value, global);
	global->Release();
}

} // namespace

NCB_POST_REGIST_CALLBACK(RegisterDirList);
