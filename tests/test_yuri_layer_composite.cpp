// Host reproduction of a retail black-layer defect, at layer level.
//
// Device traces narrowed this to one unexplained pair of observations. Both
// probes report the same iTVPBaseBitmap*:
//
//   probe-fill       #9 color=00000000 800x600 back=00000000 surf=835ec410
//   probe-draw-entry #1 type=12 centre=ff000000 800x600 surf=835ec410
//
// The layer is filled transparent and, on entry to Draw(), reads opaque black.
// "Same surf" does not say which of two things happened, because surf is the
// bitmap *wrapper* and its internal texture pointer is replaced by Independ(),
// Recreate(), SetSize() and AssignTexture():
//
//   (a) the layer's pixels were overwritten in place, or
//   (b) the layer's bitmap was repointed at a different, opaque texture.
//
// This test models the affected title's tree -- an ltOpaque primary with a
// full-screen ltAddAlpha child, which is what `@position left=0 top=0
// width=800 height=600 frame="" opacity=0` builds -- runs the real compositor,
// and records the pixel *and* the texture pointer on both sides. That
// distinguishes (a) from (b) directly.
//
// It runs against the generated Vita sources, so the compositor under test is
// the product's own.

#include "tjsCommHead.h"

// tTJSNI_BaseLayer keeps Manager, Draw() and CompleteForWindow() private and
// grants friendship to tTVPLayerManager; tTVPLayerManager in turn has a private
// destructor because it is reference counted. Normal construction runs through
// tTJSNI_BaseLayer::Construct, which needs a TJS window object, a native-class
// registration and a layer tree owner closure -- none of which exist here and
// none of which affect compositing.
//
// Opening access for this translation unit only is the smallest way to drive
// the real compositor. Access control affects neither object layout nor name
// mangling, so the other translation units -- which compile these headers
// normally -- link against exactly the same code. This is a harness, not
// product code, and the trick is confined to this file.
#define private public
#define protected public
#include "LayerIntf.h"
#include "LayerManager.h"
#undef protected
#undef private

#include "LayerTreeOwner.h"
#include "RenderManager.h"

#include <cstdio>

namespace {

int failures = 0;

void fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

constexpr tjs_uint32 kTransparentFill = 0x00000000u;
constexpr tjs_uint32 kOpaqueBlack = 0xff000000u;
constexpr tjs_uint32 kBackgroundArt = 0xff2080c0u; // a recognisable colour

constexpr tjs_int kWidth = 800;
constexpr tjs_int kHeight = 600;

// The layer tree needs an owner for its UI callbacks. None of them participate
// in compositing; a resize or image-change notification is merely recorded so
// the test can assert the draw cycle actually ran.
class HarnessTreeOwner : public iTVPLayerTreeOwner {
public:
    int image_changes = 0;

    void TJS_INTF_METHOD RegisterLayerManager(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD UnregisterLayerManager(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD StartBitmapCompletion(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD NotifyBitmapCompleted(iTVPLayerManager*, tjs_int,
                                               tjs_int, tTVPBaseTexture*,
                                               const tTVPRect&, tTVPLayerType,
                                               tjs_int) override {}
    void TJS_INTF_METHOD EndBitmapCompletion(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD SetMouseCursor(iTVPLayerManager*, tjs_int) override {}
    void TJS_INTF_METHOD GetCursorPos(iTVPLayerManager*, tjs_int& x,
                                      tjs_int& y) override { x = y = 0; }
    void TJS_INTF_METHOD SetCursorPos(iTVPLayerManager*, tjs_int,
                                      tjs_int) override {}
    void TJS_INTF_METHOD ReleaseMouseCapture(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD SetHint(iTVPLayerManager*, iTJSDispatch2*,
                                 const ttstr&) override {}
    void TJS_INTF_METHOD NotifyLayerResize(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD NotifyLayerImageChange(iTVPLayerManager*) override {
        ++image_changes;
    }
    void TJS_INTF_METHOD SetAttentionPoint(iTVPLayerManager*,
                                           tTJSNI_BaseLayer*, tjs_int,
                                           tjs_int) override {}
    void TJS_INTF_METHOD DisableAttentionPoint(iTVPLayerManager*) override {}
    void TJS_INTF_METHOD SetImeMode(iTVPLayerManager*, tjs_int) override {}
    void TJS_INTF_METHOD ResetImeMode(iTVPLayerManager*) override {}
    iTJSDispatch2* TJS_INTF_METHOD GetOwnerNoAddRef() const override {
        return nullptr;
    }
};

// tTJSNI_BaseLayer is concrete and its constructor only zeroes members, so a
// layer can be built without a TJS engine, window object or class
// registration.
class HarnessLayer : public tTJSNI_BaseLayer {
public:
    iTVPBaseBitmap* image() { return MainImage; }

    // The texture behind the bitmap wrapper. This is the value that
    // distinguishes "pixels overwritten" from "bitmap repointed".
    const void* texture() {
        return MainImage ? static_cast<const void*>(MainImage->GetTexture())
                         : nullptr;
    }

    void attach(tTVPLayerManager* manager, HarnessLayer* parent) {
        Manager = manager;
        if (parent) SetParent(parent);
    }
};

struct Sample {
    tjs_uint32 pixel;
    const void* texture;
};

Sample sample(HarnessLayer& layer) {
    Sample s{0xdeadbeefu, layer.texture()};
    if (layer.image()) {
        s.pixel = layer.image()->GetPoint(kWidth / 2, kHeight / 2);
    }
    return s;
}

void report(const char* stage, const Sample& s) {
    std::printf("  %-22s pixel=%08x texture=%p\n", stage,
                static_cast<unsigned>(s.pixel), s.texture);
}

} // namespace

extern void TVPInitTVPGL();

int main() {
    TVPInitTVPGL();
    TVPGetRenderManager(ttstr(TJS_W("software")));

    HarnessTreeOwner owner;
    tTVPLayerManager manager(&owner);
    // Reference counted; the destructor is private in normal builds.

    // Primary: KAG's fore.base, ltOpaque, holding the background art.
    HarnessLayer primary;
    primary.attach(&manager, nullptr);
    primary.SetBounds(tTVPRect(0, 0, kWidth, kHeight));
    primary.SetType(ltOpaque);
    manager.AttachPrimary(&primary);
    primary.SetVisible(true);
    if (!primary.image()) {
        fail("primary layer has no image");
        return 1;
    }
    primary.image()->Fill(tTVPRect(0, 0, kWidth, kHeight), kBackgroundArt);

    // Child: the title menu message layer. ltAddAlpha, full screen, cleared to
    // fully transparent, exactly as MessageLayer.clearLayer does at
    // frameOpacity 0.
    HarnessLayer message;
    message.attach(&manager, &primary);
    message.SetBounds(tTVPRect(0, 0, kWidth, kHeight));
    message.SetType(ltAddAlpha);
    message.SetVisible(true);
    if (!message.image()) {
        fail("message layer has no image");
        return 1;
    }
    message.image()->Fill(tTVPRect(0, 0, kWidth, kHeight), kTransparentFill);

    const Sample after_fill = sample(message);
    std::puts("message layer:");
    report("after fill", after_fill);

    if (after_fill.pixel != kTransparentFill) {
        fail("fill did not land -- harness does not model the engine faithfully");
        return 1;
    }

    // Run the real draw cycle: primary composites its children into the layer
    // manager's DrawBuffer.
    manager.UpdateToDrawDevice();

    const Sample after_draw = sample(message);
    report("after draw cycle", after_draw);

    // Determine whether the texture changed or its pixels were overwritten.
    if (after_draw.pixel == kOpaqueBlack) {
        if (after_draw.texture != after_fill.texture) {
            std::puts("REPRODUCED: bitmap was repointed at a different texture");
        } else {
            std::puts("REPRODUCED: pixels were overwritten in place");
        }
        fail("message layer turned opaque black across the draw cycle");
    } else if (after_draw.pixel != kTransparentFill) {
        std::printf("message layer changed to %08x across the draw cycle\n",
                    static_cast<unsigned>(after_draw.pixel));
        fail("message layer content changed unexpectedly");
    }

    // And the visible result: the draw buffer should show the background art
    // through a fully transparent layer, not black.
    if (tTVPBaseTexture* draw_buffer = manager.GetDrawBuffer()) {
        const tjs_uint32 presented =
            draw_buffer->GetPoint(kWidth / 2, kHeight / 2);
        std::printf("  %-22s pixel=%08x\n", "draw buffer", presented);
        if ((presented & 0x00ffffffu) == 0x00000000u) {
            fail("draw buffer is black where the background art should show");
        }
    } else {
        std::puts("  draw buffer: none (compositor did not allocate one)");
    }

    if (failures) {
        std::fprintf(stderr, "%d layer-composite check(s) failed\n", failures);
        return 1;
    }
    std::puts("Yuri layer composite checks passed");
    return 0;
}
