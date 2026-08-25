// Host reproduction harness for a retail black-layer defect.
//
// Eight hardware rounds established this much: a message layer's bitmap is
// filled correctly (0x80000000 for the message box, 0x00000000 for the title
// menu), and by the time the compositor reads it back the pixel is 0xff000000.
// A probe on the render manager's raster funnel caught the moment -- a
// FillARGB writing 0xff000000 over the surface that was just cleared -- but
// could not name the caller.
//
// 0xff000000 is not an arbitrary value. It is the layer manager's DrawBuffer
// clear colour (LayerManager.cpp:102, :112, :139, :149, :163) and nothing else
// in the visual tree fills with it. For that value to appear inside a message
// layer's bitmap, the layer's pixels and the draw buffer's pixels must be the
// same memory.
//
// Two lifetime rules make that plausible, and both are reachable here:
//
//   1. iTVPTexture2D::Release() (RenderManager.cpp:310) pushes a texture onto
//      _toDeleteTextures when RefCount == 1 but does NOT decrement RefCount.
//      A texture queued for deletion therefore still reports RefCount == 1,
//      which is exactly what IsIndependent() treats as "safe to mutate in
//      place". Deletion happens later, at RecycleProcess().
//
//   2. tTVPNativeBaseBitmap::Independ() skips its copy when IsIndependent()
//      is true, so a bitmap can keep writing into a texture that is already
//      queued for destruction.
//
// This runs entirely on the host: RenderManager, LayerBitmapIntf, tvpgl and
// LayerBitmapImpl all compile natively, and none of the suspected mechanism
// touches a Vita API. If the defect reproduces here it can be fixed and
// regression-tested without a device; if it does not, that narrows the fault
// to something genuinely Vita-specific and is worth knowing for the same
// reason.

#include "tjsCommHead.h"

#include "LayerBitmapIntf.h"
#include "RenderManager.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

void check_pixel(const iTVPBaseBitmap& bitmap, tjs_int x, tjs_int y,
                 tjs_uint32 expected, const char* what) {
    const tjs_uint32 actual = bitmap.GetPoint(x, y);
    if (actual == expected) return;
    std::fprintf(stderr, "FAIL: %s: expected %08x, got %08x\n", what,
                 static_cast<unsigned>(expected), static_cast<unsigned>(actual));
    ++failures;
}

// Kirikiri's message-box frame fill: 50% black, premultiplied.
constexpr tjs_uint32 kFrameFill = 0x80000000u;
// The title menu layer at frameOpacity 0.
constexpr tjs_uint32 kTransparentFill = 0x00000000u;
// tTVPLayerManager's draw-buffer clear.
constexpr tjs_uint32 kDrawBufferClear = 0xff000000u;

constexpr tjs_uint kMessageWidth = 608;
constexpr tjs_uint kMessageHeight = 448;
constexpr tjs_uint kPrimaryWidth = 800;
constexpr tjs_uint kPrimaryHeight = 600;

// A fill must survive being read straight back. This is the invariant the
// hardware probe verified holds on device, so a failure here would mean the
// harness is not modelling the engine faithfully.
void test_fill_round_trip() {
    tTVPBaseTexture message(kMessageWidth, kMessageHeight);
    message.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kFrameFill);
    check_pixel(message, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "message box fill round-trip");

    tTVPBaseTexture title(kPrimaryWidth, kPrimaryHeight);
    title.Fill(tTVPRect(0, 0, kPrimaryWidth, kPrimaryHeight), kTransparentFill);
    check_pixel(title, kPrimaryWidth / 2, kPrimaryHeight / 2, kTransparentFill,
                "title menu fill round-trip");
}

// The defect, stated directly: clearing an unrelated draw buffer to opaque
// black must not disturb a message layer that was already filled.
void test_draw_buffer_clear_does_not_touch_layers() {
    tTVPBaseTexture message(kMessageWidth, kMessageHeight);
    message.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kFrameFill);

    tTVPBaseTexture draw_buffer(kPrimaryWidth, kPrimaryHeight);
    draw_buffer.Fill(tTVPRect(0, 0, kPrimaryWidth, kPrimaryHeight),
                     kDrawBufferClear);

    check_pixel(message, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "message box survives draw-buffer clear");
}

// The same, with the texture recycler cycling in between. A texture freed and
// its memory handed to the next allocation is the cheapest way for two
// bitmaps to end up sharing pixels.
void test_recycled_texture_does_not_alias_live_layer() {
    tTVPBaseTexture message(kMessageWidth, kMessageHeight);
    message.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kFrameFill);

    // Churn: allocate and drop same-sized textures so the allocator is likely
    // to reuse memory, then drain the deferred-deletion queue.
    for (int round = 0; round < 4; ++round) {
        {
            tTVPBaseTexture scratch(kMessageWidth, kMessageHeight);
            scratch.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight),
                         kDrawBufferClear);
        }
        iTVPTexture2D::RecycleProcess();
    }

    check_pixel(message, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "message box survives texture recycling");
}

// Copy-on-write: two bitmaps sharing one texture must separate before either
// is written. CopyRect over the whole area takes the AssignTexture fast path
// (LayerBitmapIntf.cpp:937-948), which makes the sharing real rather than
// hypothetical.
void test_shared_texture_separates_before_write() {
    tTVPBaseTexture source(kMessageWidth, kMessageHeight);
    source.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kFrameFill);

    tTVPBaseTexture sharer(kMessageWidth, kMessageHeight);
    sharer.CopyRect(0, 0, &source, tTVPRect(0, 0, kMessageWidth, kMessageHeight));
    check_pixel(sharer, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "full-area CopyRect transfers content");

    // Now scribble opaque black on the copy. The original must not change.
    sharer.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kDrawBufferClear);
    check_pixel(source, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "writing a shared copy does not corrupt the original");
    check_pixel(sharer, kMessageWidth / 2, kMessageHeight / 2, kDrawBufferClear,
                "writing a shared copy updates the copy");
}

// Deferred destruction must not hand one heap block to two live owners.
//
// Upstream Release() queues a texture at RefCount == 1 without clearing
// RefCount, and RecycleProcess() deletes every queued texture unconditionally.
// A texture that is acquired again after being queued is therefore deleted
// while still owned, and the next allocation can land on top of it. Since
// tTVPLayerManager clears its draw buffer to 0xFF000000, a layer bitmap that
// ends up sharing that block reads opaque black.
//
// This models the hazard directly at the texture level, which is where the
// invariant lives.
void test_queued_texture_is_not_deleted_while_owned() {
    iTVPRenderManager* manager = TVPGetRenderManager(ttstr(TJS_W("software")));
    iTVPTexture2D* texture =
        manager->CreateTexture2D(nullptr, 0, kMessageWidth, kMessageHeight,
                                 TVPTextureFormat::RGBA);
    check(texture != nullptr, "texture created");
    if (!texture) return;

    // Sole owner drops it: this queues the texture for deferred destruction.
    texture->Release();

    // Something acquires it again before the drain. Under the upstream rules
    // this succeeds silently, because a queued texture still reports
    // RefCount == 1.
    texture->AddRef();

    // Draining now must not destroy a texture that has a live owner.
    iTVPTexture2D::RecycleProcess();

    // Touch it. With the invariant unsound this is a use-after-free; the value
    // itself does not matter, only that the object is still there.
    texture->GetPoint(0, 0);
    check(static_cast<int>(texture->GetWidth()) ==
              static_cast<int>(kMessageWidth),
          "queued-then-reacquired texture survives the recycler drain");

    texture->Release();
    iTVPTexture2D::RecycleProcess();
}

// Releasing an already-queued texture must not queue it a second time; the
// drain would otherwise delete it twice and corrupt the allocator's free list.
void test_double_release_does_not_double_delete() {
    iTVPRenderManager* manager = TVPGetRenderManager(ttstr(TJS_W("software")));
    iTVPTexture2D* texture =
        manager->CreateTexture2D(nullptr, 0, kMessageWidth, kMessageHeight,
                                 TVPTextureFormat::RGBA);
    check(texture != nullptr, "texture created for double release");
    if (!texture) return;

    texture->Release();
    texture->Release(); // second release of an already-queued texture

    // A double delete here corrupts the heap rather than failing an assertion,
    // so run a drain and then allocate and use another texture: with the free
    // list damaged this is where the damage surfaces.
    iTVPTexture2D::RecycleProcess();

    tTVPBaseTexture probe(kMessageWidth, kMessageHeight);
    probe.Fill(tTVPRect(0, 0, kMessageWidth, kMessageHeight), kFrameFill);
    check_pixel(probe, kMessageWidth / 2, kMessageHeight / 2, kFrameFill,
                "allocator intact after a double release");
}

} // namespace

extern void TVPInitTVPGL();

int main() {
    // Install the TVPGL function pointers. Without this every blend and fill
    // entry point is null, which the engine itself does from TVPSystemInit.
    TVPInitTVPGL();

    // Force the software renderer explicitly. The no-argument overload reads
    // IndividualConfigManager, which the harness answers with "software".
    TVPGetRenderManager(ttstr(TJS_W("software")));

    test_fill_round_trip();
    test_draw_buffer_clear_does_not_touch_layers();
    test_recycled_texture_does_not_alias_live_layer();
    test_shared_texture_separates_before_write();
    test_queued_texture_is_not_deleted_while_owned();
    test_double_release_does_not_double_delete();

    if (failures) {
        std::fprintf(stderr, "%d texture-aliasing check(s) failed\n", failures);
        return 1;
    }
    std::puts("Yuri texture aliasing checks passed");
    return 0;
}
