#pragma once

#include <cstdint>

namespace krkrvita {

inline constexpr std::uint64_t kYuriEngineTickUs = 16667;

constexpr std::uint32_t yuri_frame_delay_us(std::uint64_t loop_started_us,
                                             std::uint64_t now_us) {
    if (now_us <= loop_started_us) return static_cast<std::uint32_t>(kYuriEngineTickUs);
    const std::uint64_t elapsed = now_us - loop_started_us;
    return elapsed < kYuriEngineTickUs
               ? static_cast<std::uint32_t>(kYuriEngineTickUs - elapsed)
               : 0;
}

} // namespace krkrvita
