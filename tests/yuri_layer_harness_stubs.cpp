// Layer-stack extension to the host harness.
//
// tests/yuri_texture_harness_stubs.cpp covers the bitmap and render layer.
// This file adds what tTJSNI_BaseLayer and tTVPLayerManager additionally pull
// in: graphics loading, event dispatch, cursors, transition providers and the
// TJS-facing Layer class. None of those participate in "fill a layer, draw the
// tree, read the result", which is the whole scope of the reproduction.
//
// As in the bitmap stubs, anything that must not be reached aborts rather than
// returning a plausible value. TVPToActualColor / TVPFromActualColor are the
// exception: they are on the fill path and are implemented for real (identity
// for 32bpp, which is what Kirikiri does when there is no palette).

#include "tjsCommHead.h"

#include "LayerIntf.h"
#include "LayerManager.h"
#include "GraphicsLoaderIntf.h"
#include "BitmapIntf.h"
#include "TransIntf.h"
#include "EventIntf.h"
#include "MsgIntf.h"

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void layer_unreachable(const char* what) {
    std::fprintf(stderr,
                 "harness: %s was called; the layer harness does not model it\n",
                 what);
    std::abort();
}

} // namespace

// --- On the fill path: real ------------------------------------------------

tjs_uint32 TVPToActualColor(tjs_uint32 color) { return color; }
tjs_uint32 TVPFromActualColor(tjs_uint32 color) { return color; }

tjs_uint32 TVPGetTickCount() { return 0; }
void TVPStartTickCount() {}

// --- Vita profiling and damage hooks: no-ops ------------------------------

extern "C" void krkrvita_yuri_begin_frame_damage(tjs_int, tjs_int) {}
extern "C" void krkrvita_yuri_add_frame_damage(tjs_int, tjs_int, tjs_int, tjs_int) {}
extern "C" unsigned long long krkrvita_yuri_profile_now_us() { return 0; }
extern "C" void krkrvita_yuri_profile_compositor(unsigned long long,
                                                 unsigned long long,
                                                 unsigned long long) {}

// Message globals are NOT defined here. vendor/yuri/src/core/msg/MsgIntf.cpp
// defines all of them as tTJSMessageHolder via the TVP_MSG_DECL_CONST macro;
// hand-declaring them as ttstr produces conflicting-declaration errors. Link
// MsgIntf.cpp instead.
// TVPEventDisabled is a bool flag, not a message string.
bool TVPEventDisabled = false;
// The shared simple image provider instance used by transitions.
// TVPSimpleImageProvider is defined by TransIntf.cpp.

// --- TJS-facing Layer class ------------------------------------------------
//
// tTJSNC_Layer is the script-visible class object. The harness drives layers
// through their C++ interface, so the class object is never registered; these
// definitions exist only to give the vtable a home.

// tTJSNC_Layer::ClassID and its constructor are defined by LayerIntf.cpp.
// Only CreateNativeInstance is left to the platform, which is why it is the
// one member stubbed here.

tTJSNativeInstance* tTJSNC_Layer::CreateNativeInstance() {
    layer_unreachable("tTJSNC_Layer::CreateNativeInstance");
}

tjs_uint32 tTJSNC_Bitmap::ClassID = (tjs_uint32)-1;

void tTJSNI_Bitmap::CopyFrom(const iTVPBaseBitmap*) {
    layer_unreachable("tTJSNI_Bitmap::CopyFrom");
}

// --- Graphics loading, events, cursors, transitions: never reached ---------

void TVPCancelSourceEvents(iTJSDispatch2*) {}

iTJSDispatch2* TVPCreateEventObject(const tjs_char*, iTJSDispatch2*,
                                    iTJSDispatch2*) {
    layer_unreachable("TVPCreateEventObject");
}

iTJSDispatch2* TVPCreateRectObject(tjs_int, tjs_int, tjs_int, tjs_int) {
    layer_unreachable("TVPCreateRectObject");
}

void TVPPostEvent(iTJSDispatch2*, iTJSDispatch2*, ttstr&, tjs_uint32,
                  tjs_uint32, tjs_uint, tTJSVariant*) {}

// TVPFormatMessage comes from MsgIntf.cpp.

// The (msg, ttstr, int) overload of TVPThrowExceptionMessage is in MsgIntf.cpp.

// --- Remaining layer-stack dependencies -----------------------------------
//
// Transitions, graphic loading, cursors and the "about" strings. None are on
// the fill/composite path; each aborts if reached so the harness cannot
// silently test a fiction.

ttstr TVPActionName(TJS_W("action"));

// TVPFindTransHandlerProvider is defined by TransIntf.cpp, which is linked in.

tjs_int TVPGetCursor(const ttstr&) { layer_unreachable("TVPGetCursor"); }

ttstr TVPGetImportantLog() { return ttstr(); }
void TVPGetVersion() {}
ttstr TVPReadAboutStringFromResource() { return ttstr(); }

int TVPLoadGraphic(iTVPBaseBitmap*, const ttstr&, tjs_int, tjs_uint, tjs_uint,
                   tTVPGraphicLoadMode, ttstr*, iTJSDispatch2**) {
    layer_unreachable("TVPLoadGraphic");
}

void TVPLoadGraphicProvince(tTVPBaseBitmap*, const ttstr&, tjs_int, tjs_uint,
                            tjs_uint) {
    layer_unreachable("TVPLoadGraphicProvince");
}

void TVPSaveImage(const ttstr&, const ttstr&, const iTVPBaseBitmap*,
                  iTJSDispatch2*) {
    layer_unreachable("TVPSaveImage");
}

// tTVPScanLineProviderForBaseBitmap, tTVPSimpleOptionProvider and
// tTVPSimpleImageProvider are defined by TransIntf.cpp, which is linked in.
// Defining their constructors here instead leaves the vtables undefined.
