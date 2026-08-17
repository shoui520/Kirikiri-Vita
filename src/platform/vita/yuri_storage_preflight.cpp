#include "tjsCommHead.h"

#include "StorageIntf.h"
#include "StorageImpl.h"
#include "ScriptMgnIntf.h"
#include "SysInitImpl.h"
#include "MsgIntf.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/system_app_id_compat.hpp"
#include "krkrvita/vita_storage_path.hpp"
#include "krkrvita/yuri_storage_preflight.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Yuri implements this media-dispatch helper but omits it from StorageIntf.h.
void TVPGetListAt(const ttstr& name, iTVPStorageLister* lister);

namespace {

class ProjectStorageLister final : public iTVPStorageLister {
public:
    void TJS_INTF_METHOD Add(const ttstr& file) override {
        files.push_back(file);
    }

    std::vector<ttstr> files;
};

void require_openable(const ttstr& path, const char* marker) {
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(path, TJS_BS_READ));
    if (!stream) TVPThrowExceptionMessage(TJS_W("Cannot open Vita storage"), path);
    krkrvita_boot_trace(marker);
}

bool has_suffix(const ttstr& value, const tjs_char* suffix) {
    const tjs_int value_length = value.GetLen();
    const tjs_int suffix_length = TJS_strlen(suffix);
    return value_length >= suffix_length &&
        !TJS_strcmp(value.c_str() + value_length - suffix_length, suffix);
}

} // namespace

ttstr krkrvita_yuri_select_project(const ttstr& native_project_root) {
    const std::u16string directory = krkrvita::vita_directory_path(
        std::u16string_view(native_project_root.c_str(), native_project_root.GetLen()));
    const ttstr root(directory);

    const ttstr content_data = root + TJS_W("content-data");
    if (TVPCheckExistentLocalFolder(content_data)) {
        krkrvita_boot_trace("retail-project-content-data-selected");
        return content_data + TJS_W("/");
    }

    const ttstr data_xp3 = root + TJS_W("data.xp3");
    if (TVPCheckExistentLocalFile(data_xp3)) {
        krkrvita_boot_trace("retail-project-data-xp3-selected");
        return data_xp3;
    }

    const ttstr data_exe = root + TJS_W("data.exe");
    if (TVPCheckExistentLocalFile(data_exe)) {
        krkrvita_boot_trace("retail-project-data-exe-selected");
        return data_exe;
    }

    const ttstr data_directory = root + TJS_W("data");
    if (TVPCheckExistentLocalFolder(data_directory)) {
        krkrvita_boot_trace("retail-project-data-directory-selected");
        return data_directory + TJS_W("/");
    }

    krkrvita_boot_trace("retail-project-root-selected");
    return root;
}

void krkrvita_yuri_storage_preflight(const ttstr& native_project_path) {
    krkrvita_boot_trace("yuri-storage-preflight-entered");

    const std::u16string native = krkrvita::vita_directory_path(
        std::u16string_view(native_project_path.c_str(), native_project_path.GetLen()));
    const ttstr project(native);
    ProjectStorageLister lister;
    TVPGetListAt(TVPNormalizeStorageName(project), &lister);
    if (lister.files.empty())
        TVPThrowExceptionMessage(TJS_W("Vita game directory is not enumerable"), project);
    krkrvita_boot_trace("yuri-project-directory-enumerated");

    auto archive = std::find(lister.files.begin(), lister.files.end(), TJS_W("data.xp3"));
    if (archive == lister.files.end()) {
        archive = std::find_if(lister.files.begin(), lister.files.end(),
            [](const ttstr& file) { return has_suffix(file, TJS_W(".xp3")); });
    }
    if (archive != lister.files.end())
        require_openable(project + *archive, "yuri-project-xp3-opened");

    tTJSVariant option;
    if (TVPGetCommandLine(TJS_W("-xp3filter"), &option))
        require_openable(ttstr(option), "yuri-xp3filter-opened");
    if (TVPGetCommandLine(TJS_W("-krkrpatch"), &option))
        require_openable(ttstr(option), "yuri-patch-opened");

    krkrvita_boot_trace("yuri-storage-preflight-complete");
}

void krkrvita_yuri_startup_storage_preflight() {
    krkrvita_boot_trace("yuri-startup-storage-preflight-entered");
    tTJS* engine = TVPGetScriptEngine();
    if (!engine || !krkrvita::install_system_app_id_compat(*engine)) {
        TVPThrowExceptionMessage(
            TJS_W("Cannot install System.checkAppId compatibility"));
    }
    krkrvita_boot_trace("yuri-system-app-id-compat-ready");
    require_openable(TJS_W("startup.tjs"), "yuri-startup-storage-opened");
    krkrvita_boot_trace("yuri-startup-storage-preflight-complete");
}
