// Minimal environment for the host texture-aliasing harness.
//
// tests/test_yuri_texture_aliasing.cpp links Yuri's real RenderManager,
// LayerBitmapIntf, LayerBitmapImpl and tvpgl so the texture lifetime and
// copy-on-write logic under test is the genuine article. Those units reference
// a good deal of the surrounding engine -- fonts, the config manager, the
// resampler, message-box plumbing -- none of which participates in a Fill or a
// CopyRect.
//
// Everything stubbed here is stubbed because the harness must never reach it.
// The stubs abort or throw rather than returning plausible values, so if the
// code under test ever does depend on one, the harness fails loudly instead of
// quietly testing a fiction. The one exception is TVPExecThreadTask, which is
// implemented for real, serially: the render methods genuinely call it to
// split work, and running the tasks inline is a faithful single-core execution
// rather than a stub.

#include "tjsCommHead.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "ComplexRect.h"
#include "LayerBitmapIntf.h"
#include "EventIntf.h"

namespace {

[[noreturn]] void unreachable(const char* what) {
    std::fprintf(stderr,
                 "harness: %s was called, but the texture-aliasing harness is "
                 "not meant to reach it\n",
                 what);
    std::abort();
}

} // namespace

// --- Thread dispatch: real, executed serially -----------------------------

void TVPExecThreadTask(int num, const std::function<void(int)>& func) {
    if (num < 1) num = 1;
    for (int index = 0; index < num; ++index) func(index);
}

tjs_int TVPGetThreadNum() { return 1; }

// --- Error reporting ------------------------------------------------------

// TVPThrowExceptionMessage and the message-string globals come from
// vendor/yuri/src/core/msg/MsgIntf.cpp, which both harnesses link. Defining
// them here as well is a multiple-definition link error, and defining the
// globals as ttstr rather than tTJSMessageHolder is a type conflict.


// --- Event hooks: no-ops --------------------------------------------------

void TVPAddCompactEventHook(tTVPCompactEventCallbackIntf*) {}
void TVPRemoveCompactEventHook(tTVPCompactEventCallbackIntf*) {}
void TVPAddContinuousEventHook(tTVPContinuousEventCallbackIntf*) {}
void TVPRemoveContinuousEventHook(tTVPContinuousEventCallbackIntf*) {}
void TVPAddAtExitHandler(tjs_int, void (*)()) {}
void TVPCheckMemory() {}
void TVPInitWindowOptions() {}

// --- Paths, fonts, resampling: never reached ------------------------------

ttstr TVPSearchPlacedPath(const ttstr&) { unreachable("TVPSearchPlacedPath"); }

// MsgIntf.cpp declares but does not define this; the real one lives in the
// platform's window layer.
void TVPShowSimpleMessageBox(const ttstr&, const ttstr&) {
    unreachable("TVPShowSimpleMessageBox");
}

void TVPResampleImage(const tTVPRect&, iTVPBaseBitmap*, const tTVPRect&,
                      const iTVPBaseBitmap*, const tTVPRect&, tTVPBBStretchType,
                      double, tTVPBBBltMethod, tjs_int, bool) {
    unreachable("TVPResampleImage");
}

// --- Config manager -------------------------------------------------------
//
// iTVPBaseBitmap::Fill resolves its render method through the no-argument
// TVPGetRenderManager(), which asks the config manager which renderer to use.
// Answer "software" and refuse everything else, so the harness exercises the
// same software path the Vita build runs.

#include "IndividualConfigManager.h"

IndividualConfigManager* IndividualConfigManager::GetInstance() {
    static IndividualConfigManager instance;
    return &instance;
}

template <>
std::string IndividualConfigManager::GetValue<std::string>(
    const std::string& name, const std::string& defVal) {
    if (name == "renderer") return "software";
    return defVal;
}

template <>
bool IndividualConfigManager::GetValue<bool>(const std::string& name,
                                             const bool& defVal) {
    (void)name;
    return defVal;
}

std::string IndividualConfigManager::GetFilePath() {
    unreachable("IndividualConfigManager::GetFilePath");
}

void IndividualConfigManager::Clear() {}

// --- Storage, logging and font plumbing: never reached --------------------

void TVPAddLog(const ttstr&) {}

// A bitmap records a font name at construction. Nothing in Fill or CopyRect
// rasterizes a glyph, so any name will do; answering is more faithful than
// aborting, because the real engine always has one.
ttstr TVPGetDefaultFontName() { return ttstr(TJS_W("MS Gothic")); }

void TVPGetAllFontList(std::vector<ttstr>&) { unreachable("TVPGetAllFontList"); }

tTJSBinaryStream* TVPCreateBinaryStreamForRead(const ttstr&, const ttstr&) {
    unreachable("TVPCreateBinaryStreamForRead");
}

tTJSBinaryStream* TVPCreateFontStream(const ttstr&) {
    unreachable("TVPCreateFontStream");
}

void TVPEncodeUTF8ToUTF16(ttstr&, const std::string&) {
    unreachable("TVPEncodeUTF8ToUTF16");
}


// --- Vita platform edge -----------------------------------------------------
//
// The generated RenderManager.cpp is the product's own code, so it reaches for
// the Vita bitmap allocator and the boot tracer. Both are substituted here with
// the plainest host equivalents: the allocator's job in the product is to place
// large bitmaps in USER_RW memblocks, which changes where memory comes from but
// not the aliasing and copy-on-write behaviour under test.

#include <cstddef>

namespace krkrvita {

void* vita_bitmap_allocate(std::size_t bytes) { return std::malloc(bytes); }

void vita_bitmap_deallocate(void* ptr) { std::free(ptr); }

} // namespace krkrvita

extern "C" void krkrvita_boot_trace(const char* message) {
    std::fprintf(stderr, "trace: %s\n", message ? message : "(null)");
}

void krkrvita_compact_ms_gothic_pread_cache() noexcept {}

void TVPDeliverCompactEvent(tjs_int) {}
