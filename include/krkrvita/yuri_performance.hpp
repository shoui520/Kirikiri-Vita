#pragma once

#include <cstdint>

namespace krkrvita {

struct YuriPerformanceSnapshot {
    std::uint64_t compositor_calls = 0;
    std::uint64_t compositor_us_total = 0;
    std::uint64_t compositor_us_max = 0;
    std::uint64_t dirty_rects_total = 0;
    std::uint64_t dirty_rects_max = 0;
    std::uint64_t dirty_pixels_total = 0;
    std::uint64_t dirty_pixels_max = 0;
    std::uint64_t normal_window_updates = 0;
    std::uint64_t full_window_updates = 0;
    std::uint64_t bitmap_independ_calls = 0;
    std::uint64_t bitmap_independ_copies = 0;
    std::uint64_t bitmap_independ_copy_us_total = 0;
    std::uint64_t bitmap_independ_copy_us_max = 0;
    std::uint64_t bitmap_independ_copy_bytes_total = 0;
    std::uint64_t bitmap_independ_copy_bytes_max = 0;
};

YuriPerformanceSnapshot yuri_performance_snapshot();

} // namespace krkrvita

extern "C" std::uint64_t krkrvita_yuri_profile_now_us();
extern "C" void krkrvita_yuri_profile_compositor(
    std::uint64_t elapsed_us, std::uint64_t dirty_rects,
    std::uint64_t dirty_pixels);
extern "C" void krkrvita_yuri_profile_window_update(int full_exposure);
extern "C" void krkrvita_yuri_profile_bitmap_independ(
    int copied, std::uint64_t elapsed_us, std::uint64_t copied_bytes);
// LayerManager records the exact software-compositor damage from
// NotifyUpdateRegionFixed(), after BeforeCompletion() has added onPaint and
// transition damage but before InternalComplete2() consumes it. The Vita
// window adapter uses this to avoid uploading an unchanged 1280x960
// framebuffer for every text glyph.
extern "C" void krkrvita_yuri_begin_frame_damage(int width, int height);
extern "C" void krkrvita_yuri_add_frame_damage(
    int left, int top, int right, int bottom);
