#include "krkrvita/yuri_performance.hpp"

#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <atomic>

namespace {

struct Counters {
    std::atomic<std::uint64_t> compositor_calls{0};
    std::atomic<std::uint64_t> compositor_us_total{0};
    std::atomic<std::uint64_t> compositor_us_max{0};
    std::atomic<std::uint64_t> dirty_rects_total{0};
    std::atomic<std::uint64_t> dirty_rects_max{0};
    std::atomic<std::uint64_t> dirty_pixels_total{0};
    std::atomic<std::uint64_t> dirty_pixels_max{0};
    std::atomic<std::uint64_t> normal_window_updates{0};
    std::atomic<std::uint64_t> full_window_updates{0};
    std::atomic<std::uint64_t> bitmap_independ_calls{0};
    std::atomic<std::uint64_t> bitmap_independ_copies{0};
    std::atomic<std::uint64_t> bitmap_independ_copy_us_total{0};
    std::atomic<std::uint64_t> bitmap_independ_copy_us_max{0};
    std::atomic<std::uint64_t> bitmap_independ_copy_bytes_total{0};
    std::atomic<std::uint64_t> bitmap_independ_copy_bytes_max{0};
};

Counters counters;

extern "C" void krkrvita_boot_trace(const char* message);

void update_max(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    std::uint64_t previous = target.load(std::memory_order_relaxed);
    while (previous < value &&
           !target.compare_exchange_weak(previous, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
}

} // namespace

extern "C" std::uint64_t krkrvita_yuri_profile_now_us() {
    return sceKernelGetProcessTimeWide();
}

extern "C" void krkrvita_yuri_profile_compositor(
    std::uint64_t elapsed_us, std::uint64_t dirty_rects,
    std::uint64_t dirty_pixels) {
    counters.compositor_calls.fetch_add(1, std::memory_order_relaxed);
    counters.compositor_us_total.fetch_add(elapsed_us, std::memory_order_relaxed);
    update_max(counters.compositor_us_max, elapsed_us);
    counters.dirty_rects_total.fetch_add(dirty_rects,
                                         std::memory_order_relaxed);
    update_max(counters.dirty_rects_max, dirty_rects);
    counters.dirty_pixels_total.fetch_add(dirty_pixels,
                                          std::memory_order_relaxed);
    update_max(counters.dirty_pixels_max, dirty_pixels);
}

extern "C" void krkrvita_yuri_profile_window_update(int full_exposure) {
    (full_exposure ? counters.full_window_updates
                   : counters.normal_window_updates)
        .fetch_add(1, std::memory_order_relaxed);
}

extern "C" void krkrvita_yuri_profile_bitmap_independ(
    int copied, std::uint64_t elapsed_us, std::uint64_t copied_bytes) {
    counters.bitmap_independ_calls.fetch_add(1, std::memory_order_relaxed);
    if (!copied) return;
    counters.bitmap_independ_copies.fetch_add(1, std::memory_order_relaxed);
    counters.bitmap_independ_copy_us_total.fetch_add(elapsed_us,
                                                      std::memory_order_relaxed);
    update_max(counters.bitmap_independ_copy_us_max, elapsed_us);
    counters.bitmap_independ_copy_bytes_total.fetch_add(
        copied_bytes, std::memory_order_relaxed);
    update_max(counters.bitmap_independ_copy_bytes_max, copied_bytes);
}

namespace krkrvita {

YuriPerformanceSnapshot yuri_performance_snapshot() {
    YuriPerformanceSnapshot result;
    result.compositor_calls =
        counters.compositor_calls.load(std::memory_order_relaxed);
    result.compositor_us_total =
        counters.compositor_us_total.load(std::memory_order_relaxed);
    result.compositor_us_max =
        counters.compositor_us_max.load(std::memory_order_relaxed);
    result.dirty_rects_total =
        counters.dirty_rects_total.load(std::memory_order_relaxed);
    result.dirty_rects_max =
        counters.dirty_rects_max.load(std::memory_order_relaxed);
    result.dirty_pixels_total =
        counters.dirty_pixels_total.load(std::memory_order_relaxed);
    result.dirty_pixels_max =
        counters.dirty_pixels_max.load(std::memory_order_relaxed);
    result.normal_window_updates =
        counters.normal_window_updates.load(std::memory_order_relaxed);
    result.full_window_updates =
        counters.full_window_updates.load(std::memory_order_relaxed);
    result.bitmap_independ_calls =
        counters.bitmap_independ_calls.load(std::memory_order_relaxed);
    result.bitmap_independ_copies =
        counters.bitmap_independ_copies.load(std::memory_order_relaxed);
    result.bitmap_independ_copy_us_total =
        counters.bitmap_independ_copy_us_total.load(std::memory_order_relaxed);
    result.bitmap_independ_copy_us_max =
        counters.bitmap_independ_copy_us_max.load(std::memory_order_relaxed);
    result.bitmap_independ_copy_bytes_total =
        counters.bitmap_independ_copy_bytes_total.load(
            std::memory_order_relaxed);
    result.bitmap_independ_copy_bytes_max =
        counters.bitmap_independ_copy_bytes_max.load(std::memory_order_relaxed);
    return result;
}

} // namespace krkrvita
