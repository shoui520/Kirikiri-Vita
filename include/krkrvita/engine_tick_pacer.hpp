#pragma once

#include <cstdint>

namespace krkrvita {

inline constexpr std::uint64_t kYuriEngineTickUs = 16667;
inline constexpr std::uint64_t kYuriPerformanceSnapshotQuietUs = 250000;

constexpr std::uint32_t yuri_frame_delay_us(std::uint64_t loop_started_us,
                                             std::uint64_t now_us) {
    if (now_us <= loop_started_us) return static_cast<std::uint32_t>(kYuriEngineTickUs);
    const std::uint64_t elapsed = now_us - loop_started_us;
    return elapsed < kYuriEngineTickUs
               ? static_cast<std::uint32_t>(kYuriEngineTickUs - elapsed)
               : 0;
}

// Vita storage can pause the calling thread for much longer than the tiny
// telemetry payload suggests. Never perform that diagnostic write while the
// game is actively producing frames; retain the due snapshot until a genuine
// quarter-second visual idle period instead.
constexpr bool yuri_performance_snapshot_is_safe(
    std::uint64_t now_us, std::uint64_t snapshot_due_us,
    std::uint64_t last_presented_frame_us) {
    if (now_us < snapshot_due_us || now_us < last_presented_frame_us)
        return false;
    return now_us - last_presented_frame_us >=
           kYuriPerformanceSnapshotQuietUs;
}

} // namespace krkrvita
