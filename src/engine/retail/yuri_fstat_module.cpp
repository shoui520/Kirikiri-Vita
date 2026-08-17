#include "tjsCommHead.h"

#include "StorageImpl.h"
#include "StorageIntf.h"
#include "tjsArray.h"
#include "ncbind/ncbind.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#define NCB_MODULE_NAME TJS_W("fstat.dll")

namespace {

tjs_error fstat_dirlist(tTJSVariant* result, tjs_int numparams,
                        tTJSVariant** param, iTJSDispatch2*) {
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;

    ttstr directory(*param[0]);
    if (directory.GetLastChar() != TJS_W('/')) {
        TVPThrowExceptionMessage(
            TJS_W("'/' must be specified at the end of given directory name."));
    }
    directory = TVPNormalizeStorageName(directory);

    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!result) {
        array->Release();
        return TJS_S_OK;
    }
    try {
        tTJSArrayNI* native = nullptr;
        const auto status = array->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
            reinterpret_cast<iTJSNativeInstance**>(&native));
        if (TJS_FAILED(status) || !native) {
            array->Release();
            return status;
        }
        TVPGetLocalName(directory);
        TVPGetLocalFileListAt(
            directory, [native](const ttstr& name, tTVPLocalFileInfo* info) {
                if (info && (info->Mode & (S_IFREG | S_IFDIR)))
                    native->Items.emplace_back(name);
            });
        *result = tTJSVariant(array, array);
        array->Release();
    } catch (...) {
        array->Release();
        throw;
    }
    return TJS_S_OK;
}

void mark_fstat_ready() {
    krkrvita_boot_trace("retail-fstat-ready");
}

} // namespace

NCB_ATTACH_FUNCTION(dirlist, Storages, fstat_dirlist);
NCB_POST_REGIST_CALLBACK(mark_fstat_ready);
