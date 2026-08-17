#pragma once

namespace krkrvita {

// Two persistent Vita workers still require wakeups and a completion barrier.
// Keep normal KAG dirty rectangles inline, while allowing a 512x512-or-larger
// primitive (well below the 1280x960 game surface) to amortize that barrier.
inline constexpr int hybrid_render_task_pixel_threshold = 512 * 512;

constexpr int select_adaptive_render_task_count(
    int pixel_count, float operation_factor, int row_count,
    int available_threads) noexcept {
    const int usable_threads = available_threads < row_count
        ? available_threads
        : row_count;
    if (usable_threads <= 1 ||
        pixel_count < hybrid_render_task_pixel_threshold ||
        static_cast<float>(pixel_count) < operation_factor * 500.0f)
        return 1;
    return usable_threads;
}

}  // namespace krkrvita
