#pragma once

// Compositing diagnostics for the Sharin no Kuni black-layer defect.
//
// Round one of these probes cleared the three decisions that earlier theories
// blamed: the message layer's fill writes ARGB 0x00000000 / 0x80000000 through
// dfAddAlpha, the composite resolves to bmAddAlpha with hda=0 onto an ltOpaque
// destination, and only genuinely opaque layers claim update exclusions. Every
// per-composite decision the engine makes is correct.
//
// What that leaves is the arithmetic's behaviour under repetition.
// TVPAddAlphaBlend_n_a (tvpgl.h:88) computes dest*sopa>>8 where sopa is 255 for
// a fully transparent source, so 255*255>>8 == 254: each composite of a
// transparent layer darkens what is beneath it by roughly 1/256. That is stock
// Kirikiri arithmetic and harmless on Windows, because there the destination is
// re-copied from the base layer before the children blend onto it. It is fatal
// only if something re-blends onto its own previous output, which after a few
// hundred frames reaches black.
//
// Round two therefore samples pixels: for each ltAddAlpha-onto-ltOpaque
// composite it records the source pixel and the destination pixel both before
// and after the blend. If the "before" value walks steadily downward from one
// frame to the next, the destination is accumulating and the compositor is at
// fault. If "before" is stable and only "after" is dark, the fault is upstream
// in the source content.

namespace krkrvita {

// True when this composite is one the probe wants to sample. Checked before
// any pixel is read so the sampling cost is confined to the layers of
// interest; it does not consume the log budget.
bool yuri_probe_composite_wanted(int drawtype, int destlayertype);

// Reads one pixel without mutating the bitmap. Templated so this header stays
// free of Yuri's visual headers. Returns 0xdeadbeef for a missing bitmap or an
// out-of-range point, because iTVPBaseBitmap::GetPoint throws on both.
template <typename Bitmap>
unsigned int yuri_probe_sample(const Bitmap* bitmap, int x, int y) {
    if (!bitmap) return 0xdeadbeefu;
    if (x < 0 || y < 0) return 0xdeadbeefu;
    if (x >= static_cast<int>(bitmap->GetWidth())) return 0xdeadbeefu;
    if (y >= static_cast<int>(bitmap->GetHeight())) return 0xdeadbeefu;
    return bitmap->GetPoint(x, y);
}

// Reports one sampled composite, including the destination pixel either side
// of the blend.
// Emitted once at startup so a pasted log identifies its own build. Round four
// produced no probe-copyself lines and the natural first suspicion was a stale
// install; the VPK on disk proved otherwise, and the ambiguity cost a hardware
// run. Never ship a probe build without a stamp again.
void yuri_probe_build_stamp();

// Rounds three and four left a contradiction: the fill reads back 0x00000000
// from MainImage, the composite reads 0xff000000, CopySelf is never reached
// for an ltAddAlpha layer, and CopySelf is the only thing that could have
// interposed a different surface. At most two of those can describe the same
// bitmap.
//
// surface is the address of the bitmap each probe touched -- MainImage at fill
// time, src at composite time. Matching addresses mean one surface that gets
// opacified between the two events. Differing addresses mean the compositor is
// reading a bitmap that was never filled, and the fill probe has been watching
// the wrong object all along (the other KAG page's message layer is the
// obvious candidate: title2.ks draws the menu on page=back).
void yuri_probe_composite(int drawtype, int destlayertype, int met, int opacity,
                          int hda, int width, int height, unsigned int src_pixel,
                          unsigned int dest_before, unsigned int dest_after,
                          const void* surface);

// Logs every CopySelf entry regardless of layer type, so "no lines" becomes a
// statement about CopySelf rather than about the filters.
void yuri_probe_copyself_entry(int displaytype, int width, int height);

// Round six proved the fill and the composite touch the SAME bitmap
// (surf=835ec410 in both), so the layer's own image is opacified between the
// two events. This probe samples MainImage on entry to Draw(), before the
// compositor touches anything this frame, which bisects the remaining window:
//
//   entry already ff000000  -> the surface was opacified outside the draw
//                              pass, i.e. by script-driven layer operations
//   entry 00000000          -> it is opacified during the draw pass itself
//
// One patch site, and it separates two halves of the engine rather than
// testing one more named suspect.
void yuri_probe_draw_entry(int displaytype, unsigned int centre, int width,
                           int height, const void* surface);

// Samples the middle of a bitmap using its own dimensions.
template <typename Bitmap>
unsigned int yuri_probe_sample_centre(const Bitmap* bitmap) {
    if (!bitmap) return 0xdeadbeefu;
    return yuri_probe_sample(bitmap, static_cast<int>(bitmap->GetWidth()) / 2,
                             static_cast<int>(bitmap->GetHeight()) / 2);
}

// Round three closed the bracket: the fill lands (back == colour) but the
// compositor still reads 0xff000000. So the composite source is not the
// layer's MainImage. A layer with visible children -- and the title menu layer
// has one ButtonLayer child per menu entry -- composites from a shared temp
// bitmap that CopySelf copies MainImage into.
//
// These two probes cover the only ways that temp bitmap can end up opaque.

// CopySelf skipped the copy because UpdateExcludeRect covered the region,
// leaving whatever the shared temp bitmap held from its previous user.
void yuri_probe_copyself_skip(int displaytype, int uer_left, int uer_top,
                              int uer_right, int uer_bottom, int r_left,
                              int r_top, int r_right, int r_bottom);

// CopySelf performed the copy. read_back is the destination pixel afterwards:
// 0x00000000 means the copy is faithful and something later opacifies the temp
// bitmap; 0xff000000 means the copy itself is where alpha dies.
void yuri_probe_copyself(int displaytype, unsigned int read_back, int width,
                         int height);

// Retained from round one at a small budget, to confirm these stay correct
// while the pixel probe runs.
void yuri_probe_exclude(int displaytype, int opacity, int main_image_opaque,
                        int left, int top, int right, int bottom);
// Reports a layer rectangle fill together with the pixel read back from the
// layer's own image immediately afterwards.
//
// Round two showed the composite is innocent: the blend resolves correctly and
// faithfully renders a source pixel of 0xff000000 -- opaque black -- even
// though the fill requested 0x00000000. The destination is not accumulating
// either; its pre-blend value is byte-identical frame after frame, which rules
// out the 1/256 decay theory. So the alpha byte is lost somewhere between
// FillRect asking for it and the compositor reading it.
//
// read_back brackets that gap. Equal to colour means the fill landed and a
// later write opacifies the layer; 0xff000000 against a 0x00000000 request
// means the fill path itself drops alpha, in the render method or the texture.
void yuri_probe_fill(int drawface, int displaytype, unsigned int color,
                     int width, int height, unsigned int read_back,
                     const void* surface);

// ---------------------------------------------------------------------------
// Round eight: stop naming suspects, catch the event.
//
// Six rounds narrowed this to "bitmap 835ec410 holds 00000000 after FillRect
// and ff000000 by the time the compositor reads it". Every round since has
// picked one candidate writer, instrumented it, and been wrong -- which costs
// a full flash-install-run-collect cycle each time.
//
// iTVPRenderManager::OperateRect is the single funnel every software raster
// operation passes through: Fill, CopyRect, Blt, operateRect, text blitting,
// transitions. Sampling the target there, before and after, catches whichever
// operation opacifies the surface without having to guess its name in advance.
//
// The trigger is the defect itself: a large target whose centre pixel goes
// from non-opaque to opaque. Operations that leave alpha alone, and surfaces
// that were already opaque, cost two pixel reads and log nothing.

// Logs one opacifying raster operation. Filtered and capped inside.
void yuri_probe_raster(const char* method_name, unsigned int before,
                       unsigned int after, int width, int height,
                       const void* target);

// Scoped guard around a raster operation. Templated so this header stays free
// of Yuri's render headers; destructor-based so every exit path is covered.
template <typename Texture>
class RasterProbe {
public:
    RasterProbe(const char* name, Texture* target)
        : name_(name), target_(target), before_(0), armed_(false) {
        if (!target) return;
        if (static_cast<int>(target->GetWidth()) < 320) return;
        if (static_cast<int>(target->GetHeight()) < 200) return;
        armed_ = true;
        before_ = sample();
    }

    ~RasterProbe() {
        if (!armed_) return;
        yuri_probe_raster(name_, before_, sample(),
                          static_cast<int>(target_->GetWidth()),
                          static_cast<int>(target_->GetHeight()), target_);
    }

    RasterProbe(const RasterProbe&) = delete;
    RasterProbe& operator=(const RasterProbe&) = delete;

private:
    unsigned int sample() const {
        return target_->GetPoint(static_cast<int>(target_->GetWidth()) / 2,
                                 static_cast<int>(target_->GetHeight()) / 2);
    }

    const char* name_;
    Texture* target_;
    unsigned int before_;
    bool armed_;
};

} // namespace krkrvita
