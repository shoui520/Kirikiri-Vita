#pragma once

#include "krkrvita/vita_memory_budget.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace krkrvita {

inline constexpr std::size_t kVitaBitmapMemblockThreshold =
    1u * 1024u * 1024u;
inline constexpr std::size_t kVitaBitmapMemblockPage = 4096u;
inline constexpr std::size_t kVitaBitmapAllocationHeaderBytes = 32u;
inline constexpr std::size_t kVitaBitmapPayloadAlignment = 16u;

constexpr bool vita_bitmap_uses_memblock(std::size_t requested) {
    return requested >= kVitaBitmapMemblockThreshold;
}

constexpr std::size_t vita_bitmap_memblock_bytes(std::size_t requested) {
    constexpr std::size_t overhead = kVitaBitmapAllocationHeaderBytes +
                                     kVitaBitmapPayloadAlignment - 1;
    if (requested > std::numeric_limits<std::size_t>::max() - overhead)
        return 0;
    const std::size_t total = requested + overhead;
    if (total > std::numeric_limits<std::size_t>::max() -
                    (kVitaBitmapMemblockPage - 1))
        return 0;
    return (total + kVitaBitmapMemblockPage - 1) &
           ~(kVitaBitmapMemblockPage - 1);
}

// Whether a large bitmap may take a USER_RW memblock right now.
//
// This used to compare against a fixed 64 MiB budget, which turned a guess
// into a hard ceiling: once a 1280x720 project had ~18 full-screen surfaces
// live, every further bitmap fell back to the fixed newlib heap and then
// failed outright, even with most of the console's memory unused. Ask the
// kernel instead, and keep a reserve so bitmaps cannot starve the subsystems
// whose allocations are not recoverable.
constexpr bool vita_bitmap_memblock_budget_allows(std::size_t free_user_bytes,
                                                  std::size_t requested) {
    const std::size_t mapped = vita_bitmap_memblock_bytes(requested);
    if (mapped == 0) return false;
    if (free_user_bytes <= kVitaBitmapMemblockReserveBytes) return false;
    return mapped <= free_user_bytes - kVitaBitmapMemblockReserveBytes;
}

void* vita_bitmap_allocate(std::size_t size);
void vita_bitmap_deallocate(void* memory) noexcept;
std::uint64_t vita_bitmap_memblock_bytes_live() noexcept;

} // namespace krkrvita
