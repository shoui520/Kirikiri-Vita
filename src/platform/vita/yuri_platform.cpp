#include "tjsCommHead.h"

#include "Application.h"
#include "CharacterSet.h"
#include "Platform.h"
#include "krkrvita/retail_bootstrap.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

constexpr const char* kDataRoot = "ux0:data/krkrvita";

std::string narrow(const tjs_char* value) {
    return value ? std::string(tTJSNarrowStringHolder(value)) : std::string();
}

bool create_directory_tree(const std::string& path) {
    if (path.empty()) return false;
    std::string current;
    current.reserve(path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        current += path[index];
        if (path[index] != '/' || current == "ux0:/") continue;
        if (current.size() > 1) sceIoMkdir(current.c_str(), 0777);
    }
    if (path.back() != '/') sceIoMkdir(path.c_str(), 0777);
    SceIoStat status{};
    return sceIoGetstat(path.c_str(), &status) >= 0 &&
           SCE_S_ISDIR(status.st_mode);
}

time_t to_time_t(const SceDateTime& date) {
    time_t result = 0;
    return sceRtcGetTime_t(&date, &result) >= 0 ? result : 0;
}

void append_log(const char* text) {
    if (!text) return;
    create_directory_tree(kDataRoot);
    const SceUID file = sceIoOpen("ux0:data/krkrvita/engine.log",
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                                  0666);
    if (file < 0) return;
    sceIoWrite(file, text, std::strlen(text));
    sceIoClose(file);
}

} // namespace

void TVPGetMemoryInfo(TVPMemoryInfo& memory) {
    std::memset(&memory, 0, sizeof(memory));
    SceKernelFreeMemorySizeInfo free_memory{};
    free_memory.size = sizeof(free_memory);
    if (sceKernelGetFreeMemorySize(&free_memory) < 0) return;
    memory.MemFree = static_cast<unsigned long>(free_memory.size_user / 1024);
    memory.MemTotal = 256u * 1024u;
}

tjs_int TVPGetSystemFreeMemory() {
    TVPMemoryInfo memory{};
    TVPGetMemoryInfo(memory);
    return static_cast<tjs_int>(memory.MemFree / 1024);
}

tjs_int TVPGetSelfUsedMemory() { return 0; }

extern "C" int TVPShowSimpleMessageBox(const char* text, const char* caption,
                                         unsigned int, const char**) {
    append_log(caption ? caption : "Kirikiri Vita");
    append_log(": ");
    append_log(text ? text : "");
    append_log("\n");
    krkrvita_write_error(text ? text : "Kirikiri Vita error");
    return 0;
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption,
                            const std::vector<ttstr>& buttons) {
    std::vector<std::string> storage;
    std::vector<const char*> pointers;
    storage.reserve(buttons.size());
    pointers.reserve(buttons.size());
    for (const ttstr& button : buttons) storage.emplace_back(button.AsStdString());
    for (const std::string& button : storage) pointers.push_back(button.c_str());
    const std::string narrow_text = text.AsStdString();
    const std::string narrow_caption = caption.AsStdString();
    return TVPShowSimpleMessageBox(narrow_text.c_str(), narrow_caption.c_str(),
                                   pointers.size(), pointers.data());
}

int TVPShowSimpleInputBox(ttstr&, const ttstr&, const ttstr&,
                          const std::vector<ttstr>&) {
    return mrCancel;
}

std::vector<std::string> TVPGetDriverPath() {
    return {"ux0:data/krkrvita/games"};
}

std::vector<std::string> TVPGetAppStoragePath() { return {kDataRoot}; }

bool TVPCheckStartupPath(const std::string& path) {
    SceIoStat status{};
    return sceIoGetstat(path.c_str(), &status) >= 0;
}

std::string TVPGetPackageVersionString() { return "Kirikiri Vita 0.1"; }

void TVPExitApplication(int code) {
    if (Application) Application->Terminate();
    sceKernelExitProcess(code);
}

const std::string& TVPGetInternalPreferencePath() {
    static const std::string path = "ux0:data/krkrvita/";
    create_directory_tree(kDataRoot);
    return path;
}

bool TVPDeleteFile(const std::string& filename) {
    return sceIoRemove(filename.c_str()) >= 0;
}

bool TVPRenameFile(const std::string& from, const std::string& to) {
    return sceIoRename(from.c_str(), to.c_str()) >= 0;
}

bool TVPCopyFile(const std::string& from, const std::string& to) {
    const SceUID input = sceIoOpen(from.c_str(), SCE_O_RDONLY, 0);
    if (input < 0) return false;
    const SceUID output = sceIoOpen(to.c_str(),
                                    SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                    0666);
    if (output < 0) {
        sceIoClose(input);
        return false;
    }
    char buffer[64 * 1024];
    bool success = true;
    for (;;) {
        const SceSSize read_size = sceIoRead(input, buffer, sizeof(buffer));
        if (read_size < 0) {
            success = false;
            break;
        }
        if (read_size == 0) break;
        SceSSize offset = 0;
        while (offset < read_size) {
            const SceSSize written =
                sceIoWrite(output, buffer + offset, read_size - offset);
            if (written <= 0) {
                success = false;
                break;
            }
            offset += written;
        }
        if (!success) break;
    }
    sceIoClose(output);
    sceIoClose(input);
    return success;
}

void TVPShowIME(int, int, int, int) {}
void TVPHideIME() {}
void TVPFetchSDCardPermission() {}

void TVPRelinquishCPU() { sceKernelDelayThread(1000); }

void TVPPrintLog(const char* text) { append_log(text); }

void TVPConsoleLog(const ttstr& message, bool important) {
    if (important) append_log("[important] ");
    const std::string text = message.AsStdString();
    append_log(text.c_str());
    append_log("\n");
}

void TVPOpenPatchLibUrl() {
    append_log("Patch repository URL requested; no Vita browser handoff is "
               "available in the backend runtime.\n");
}

bool TVP_stat(const char* name, tTVP_stat& result) {
    SceIoStat status{};
    if (!name || sceIoGetstat(name, &status) < 0) return false;
    result.st_mode = SCE_S_ISDIR(status.st_mode) ? S_IFDIR : S_IFREG;
    result.st_size = static_cast<uint64_t>(status.st_size);
    result.st_atime = static_cast<uint64_t>(to_time_t(status.st_atime));
    result.st_mtime = static_cast<uint64_t>(to_time_t(status.st_mtime));
    result.st_ctime = static_cast<uint64_t>(to_time_t(status.st_ctime));
    return true;
}

bool TVP_stat(const tjs_char* name, tTVP_stat& result) {
    const std::string path = narrow(name);
    return TVP_stat(path.c_str(), result);
}

void TVP_utime(const char* name, time_t modification_time) {
    if (!name) return;
    SceIoStat status{};
    sceRtcSetTime_t(&status.st_atime, modification_time);
    sceRtcSetTime_t(&status.st_mtime, modification_time);
    sceIoChstat(name, &status, SCE_CST_AT | SCE_CST_MT);
}

void TVPSendToOtherApp(const std::string&) {}
std::string TVPGetCurrentLanguage() { return "ja_jp"; }

tjs_uint32 TVPGetRoughTickCount32() {
    return static_cast<tjs_uint32>(sceKernelGetProcessTimeWide() / 1000u);
}

ttstr TVPGetPlatformName() { return TJS_W("PlayStation Vita"); }
ttstr TVPGetOSName() { return TJS_W("PlayStation Vita System Software"); }

bool TVPCreateFolders(const ttstr& folder) {
    return create_directory_tree(folder.AsStdString());
}

bool TVPWriteDataToFile(const ttstr& filepath, const void* data,
                        unsigned int length) {
    const std::string path = filepath.AsStdString();
    const std::string::size_type separator = path.find_last_of('/');
    if (separator != std::string::npos)
        create_directory_tree(path.substr(0, separator));
    const SceUID file = sceIoOpen(path.c_str(),
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                  0666);
    if (file < 0) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    unsigned int offset = 0;
    while (offset < length) {
        const SceSSize written = sceIoWrite(file, bytes + offset, length - offset);
        if (written <= 0) {
            sceIoClose(file);
            return false;
        }
        offset += static_cast<unsigned int>(written);
    }
    sceIoSyncByFd(file, 0);
    sceIoClose(file);
    return true;
}

std::string TVPShowFileSelector(const std::string&, const std::string&,
                                std::string, bool) {
    return {};
}

void TVPProcessInputEvents() {}
