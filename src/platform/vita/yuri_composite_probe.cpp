#include "krkrvita/yuri_composite_probe.hpp"

#include <cstdio>

extern "C" void krkrvita_boot_trace(const char* message);

namespace krkrvita {
namespace {

// krkrvita_boot_trace opens, writes, syncs and closes boot-status.txt on every
// call, so these caps are a boot-time budget rather than a verbosity setting.
// Round three is bracketing a single question, so the composite budget shrinks
// back to a confirmation sample and the fill budget grows to cover the title
// layer's clear, which round two's budget of 8 never reached.
constexpr int kCompositeLimit = 24;
constexpr int kExcludeLimit = 4;
constexpr int kFillLimit = 24;

// ltAddAlpha == 12, ltOpaque == 1. This is exactly the message-layer-onto-base
// composite. Round one wasted its whole budget on the ltAlpha graphic layers
// fading during the title sequence and ran out before the frame of interest;
// filtering on layer type rather than size avoids repeating that.
constexpr int kLayerTypeAddAlpha = 12;
constexpr int kLayerTypeOpaque = 1;

constexpr int kCopySelfLimit = 24;

int composite_count = 0;
int exclude_count = 0;
int fill_count = 0;
int copyself_count = 0;
int copyself_entry_count = 0;
int draw_entry_count = 0;
int raster_count = 0;

constexpr int kRasterLimit = 24;

constexpr int kDrawEntryLimit = 24;

constexpr int kCopySelfEntryLimit = 16;

// Bumped on every probe build so a pasted log names its own binary.
constexpr const char* kBuildStamp = "probe-build r8 raster-funnel";

} // namespace

bool yuri_probe_composite_wanted(int drawtype, int destlayertype) {
    return composite_count < kCompositeLimit &&
           drawtype == kLayerTypeAddAlpha && destlayertype == kLayerTypeOpaque;
}

void yuri_probe_build_stamp() { krkrvita_boot_trace(kBuildStamp); }

void yuri_probe_copyself_entry(int displaytype, int width, int height) {
    if (copyself_entry_count >= kCopySelfEntryLimit) return;
    ++copyself_entry_count;
    char line[128];
    std::snprintf(line, sizeof(line),
                  "probe-copyself-entry #%d type=%d %dx%d",
                  copyself_entry_count, displaytype, width, height);
    krkrvita_boot_trace(line);
}

void yuri_probe_raster(const char* method_name, unsigned int before,
                       unsigned int after, int width, int height,
                       const void* target) {
    if (raster_count >= kRasterLimit) return;
    // The defect, stated as a predicate: a surface that was not opaque becomes
    // opaque. Anything else is normal drawing and is not worth a log line.
    if ((before >> 24) == 0xffu) return;
    if ((after >> 24) != 0xffu) return;
    ++raster_count;
    char line[192];
    std::snprintf(line, sizeof(line),
                  "probe-raster #%d %s %08x->%08x %dx%d tex=%08x",
                  raster_count, method_name ? method_name : "(unnamed)",
                  before, after, width, height,
                  static_cast<unsigned int>(reinterpret_cast<unsigned long>(target)));
    krkrvita_boot_trace(line);
}

void yuri_probe_draw_entry(int displaytype, unsigned int centre, int width,
                           int height, const void* surface) {
    if (displaytype != kLayerTypeAddAlpha) return;
    if (width < 320 || height < 200) return;
    if (draw_entry_count >= kDrawEntryLimit) return;
    ++draw_entry_count;
    char line[176];
    std::snprintf(line, sizeof(line),
                  "probe-draw-entry #%d type=%d centre=%08x %dx%d surf=%08x",
                  draw_entry_count, displaytype, centre, width, height,
                  static_cast<unsigned int>(reinterpret_cast<unsigned long>(surface)));
    krkrvita_boot_trace(line);
}

void yuri_probe_composite(int drawtype, int destlayertype, int met, int opacity,
                          int hda, int width, int height, unsigned int src_pixel,
                          unsigned int dest_before, unsigned int dest_after,
                          const void* surface) {
    if (!yuri_probe_composite_wanted(drawtype, destlayertype)) return;
    ++composite_count;
    char line[192];
    std::snprintf(line, sizeof(line),
                  "probe-composite #%d src=%d dst=%d met=%d opa=%d hda=%d "
                  "%dx%d spx=%08x dst=%08x->%08x surf=%08x",
                  composite_count, drawtype, destlayertype, met, opacity, hda,
                  width, height, src_pixel, dest_before, dest_after,
                  static_cast<unsigned int>(reinterpret_cast<unsigned long>(surface)));
    krkrvita_boot_trace(line);
}

void yuri_probe_copyself_skip(int displaytype, int uer_left, int uer_top,
                              int uer_right, int uer_bottom, int r_left,
                              int r_top, int r_right, int r_bottom) {
    if (displaytype != kLayerTypeAddAlpha) return;
    if (copyself_count >= kCopySelfLimit) return;
    ++copyself_count;
    char line[192];
    std::snprintf(line, sizeof(line),
                  "probe-copyself #%d SKIPPED type=%d uer=%d,%d,%d,%d "
                  "r=%d,%d,%d,%d",
                  copyself_count, displaytype, uer_left, uer_top, uer_right,
                  uer_bottom, r_left, r_top, r_right, r_bottom);
    krkrvita_boot_trace(line);
}

void yuri_probe_copyself(int displaytype, unsigned int read_back, int width,
                         int height) {
    if (displaytype != kLayerTypeAddAlpha) return;
    if (width < 320 || height < 200) return;
    if (copyself_count >= kCopySelfLimit) return;
    ++copyself_count;
    char line[176];
    std::snprintf(line, sizeof(line),
                  "probe-copyself #%d copied type=%d back=%08x %dx%d",
                  copyself_count, displaytype, read_back, width, height);
    krkrvita_boot_trace(line);
}

void yuri_probe_exclude(int displaytype, int opacity, int main_image_opaque,
                        int left, int top, int right, int bottom) {
    if (exclude_count >= kExcludeLimit) return;
    ++exclude_count;
    char line[160];
    std::snprintf(line, sizeof(line),
                  "probe-exclude type=%d opa=%d imgopaque=%d rect=%d,%d,%d,%d",
                  displaytype, opacity, main_image_opaque, left, top, right,
                  bottom);
    krkrvita_boot_trace(line);
}

void yuri_probe_fill(int drawface, int displaytype, unsigned int color,
                     int width, int height, unsigned int read_back,
                     const void* surface) {
    if (fill_count >= kFillLimit) return;
    if (width < 320 || height < 200) return;
    ++fill_count;
    char line[176];
    std::snprintf(line, sizeof(line),
                  "probe-fill #%d face=%d type=%d color=%08x %dx%d back=%08x "
                  "surf=%08x",
                  fill_count, drawface, displaytype, color, width, height,
                  read_back,
                  static_cast<unsigned int>(reinterpret_cast<unsigned long>(surface)));
    krkrvita_boot_trace(line);
}

} // namespace krkrvita
